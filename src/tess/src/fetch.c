#include "fetch.h"
#include "file.h"
#include "lockfile.h"
#include "manifest.h"
#include "sha256.h"
#include "tpkg.h"

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static str compute_hash(allocator *alloc, void const *data, u32 size) {
    char buf[SHA256_HEX_SIZE + 7]; /* "sha256:" + 64 hex + NUL */
    sha256_hex(data, size, buf);
    return str_init(alloc, buf);
}

static str build_download_url(allocator *alloc, str base_url, str name, str version) {
    str    filename = tl_tpkg_filename(alloc, name, version);
    size_t blen     = str_len(base_url);
    if (blen > 0 && str_buf(&base_url)[blen - 1] == '/') {
        return str_cat(alloc, base_url, filename);
    }
    return str_cat_3(alloc, base_url, S("/"), filename);
}

static int parse_embedded_package_tl(allocator *alloc, tl_tpkg_archive *archive, tl_package *out) {
    for (u32 i = 0; i < archive->entries_count; i++) {
        if (archive->entries[i].name_len == 10 && memcmp(archive->entries[i].name, "package.tl", 10) == 0) {
            platform_temp_file tf;
            if (platform_temp_file_create(&tf, ".tl")) return 1;
            if (file_write(tf.path, archive->entries[i].data, archive->entries[i].data_len)) {
                platform_temp_file_delete(&tf);
                return 1;
            }
            int rc = tl_package_parse_file(alloc, tf.path, out);
            platform_temp_file_delete(&tf);
            return rc;
        }
    }
    return 1; /* no package.tl found */
}

defarray(locked_dep_array, tl_locked_dep);
defarray(lock_edge_array, tl_lock_edge);

static int locked_dep_contains(locked_dep_array *arr, str name, str version) {
    for (u32 i = 0; i < arr->size; i++) {
        if (str_eq(arr->v[i].name, name) && str_eq(arr->v[i].version, version)) return 1;
    }
    return 0;
}

static str resolve_dep_path(allocator *alloc, char const *work_dir, str dep_path) {
    if (file_is_absolute(dep_path)) return dep_path;
    return file_path_join(alloc, str_init_static(work_dir), dep_path);
}

static int resolve_dep(allocator *alloc, str name, str version, str base_url, str parent_name,
                       str parent_version, tl_package_info const *pkg_info, tl_fetch_opts const *opts,
                       locked_dep_array *out_deps, lock_edge_array *out_edges) {
    // Skip if already resolved
    if (locked_dep_contains(out_deps, name, version)) {
        // Still record the edge from parent
        if (!str_is_empty(parent_name)) {
            tl_lock_edge edge = {
              .name        = parent_name,
              .version     = parent_version,
              .dep_name    = name,
              .dep_version = version,
            };
            array_push(*out_edges, edge);
        }
        return 0;
    }

    str   tpkg_name = tl_tpkg_filename(alloc, name, version);
    char *data      = null;
    u32   data_size = 0;
    str   hash      = str_empty();

    if (!str_is_empty(base_url)) {
        // Download from URL
        str full_url = build_download_url(alloc, base_url, name, version);
        if (opts->verbose) {
            fprintf(stderr, "Fetching: %s\n", str_cstr(&full_url));
        }
        file_url_get_ext(alloc, str_cstr(&full_url), &data, &data_size, opts->url_opts);
        if (!data) {
            fprintf(stderr, "error: failed to download '%s'\n", str_cstr(&full_url));
            return 1;
        }
        hash = compute_hash(alloc, data, data_size);

        // Save to first depend_path directory
        if (pkg_info->depend_path_count > 0) {
            str dp        = resolve_dep_path(alloc, opts->work_dir, pkg_info->depend_paths[0]);
            str save_path = str_cat_3(alloc, dp, S("/"), tpkg_name);
            if (file_write(str_cstr(&save_path), data, data_size)) {
                fprintf(stderr, "error: failed to save '%s'\n", str_cstr(&save_path));
                return 1;
            }
            if (opts->verbose) {
                fprintf(stderr, "Saved: %s\n", str_cstr(&save_path));
            }
        }
    } else {
        // Try to find locally in depend_path directories
        str local_path = str_empty();
        for (u32 i = 0; i < pkg_info->depend_path_count; i++) {
            str dp        = resolve_dep_path(alloc, opts->work_dir, pkg_info->depend_paths[i]);
            str candidate = str_cat_3(alloc, dp, S("/"), tpkg_name);
            if (file_exists(candidate)) {
                local_path = candidate;
                break;
            }
        }
        if (str_is_empty(local_path)) {
            fprintf(stderr, "error: cannot find '%s' in depend_path directories and no URL provided\n",
                    str_cstr(&tpkg_name));
            return 1;
        }
        file_read(alloc, str_cstr(&local_path), &data, &data_size);
        if (!data) {
            fprintf(stderr, "error: failed to read '%s'\n", str_cstr(&local_path));
            return 1;
        }
        hash     = compute_hash(alloc, data, data_size);
        base_url = S(""); /* no URL for local-only deps */
    }

    // Parse archive to verify metadata and find transitive deps
    tl_tpkg_archive archive;
    if (tl_tpkg_read_from_memory(alloc, data, data_size, &archive)) {
        fprintf(stderr, "error: '%s' is not a valid .tpkg archive\n", str_cstr(&tpkg_name));
        return 1;
    }

    // Verify name and version match
    if (!str_eq(archive.metadata.name, name)) {
        fprintf(stderr, "error: archive name mismatch: expected '%s', found '%s'\n", str_cstr(&name),
                str_cstr(&archive.metadata.name));
        return 1;
    }
    if (!str_eq(archive.metadata.version, version)) {
        fprintf(stderr, "error: archive version mismatch: expected '%s', found '%s'\n", str_cstr(&version),
                str_cstr(&archive.metadata.version));
        return 1;
    }

    // Record this dependency
    tl_locked_dep locked = {
      .name     = str_copy(alloc, name),
      .version  = str_copy(alloc, version),
      .base_url = str_is_empty(base_url) ? str_empty() : str_copy(alloc, base_url),
      .hash     = hash,
    };
    array_push(*out_deps, locked);

    // Record edge from parent
    if (!str_is_empty(parent_name)) {
        tl_lock_edge edge = {
          .name        = parent_name,
          .version     = parent_version,
          .dep_name    = name,
          .dep_version = version,
        };
        array_push(*out_edges, edge);
    }

    // Resolve transitive dependencies from embedded package.tl
    tl_package trans_pkg = {0};
    if (parse_embedded_package_tl(alloc, &archive, &trans_pkg) == 0) {
        for (u32 i = 0; i < trans_pkg.dep_count; i++) {
            tl_package_dep *tdep = &trans_pkg.deps[i];
            str             turl = tdep->url;
            if (resolve_dep(alloc, tdep->name, tdep->version, turl, name, version, pkg_info, opts, out_deps,
                            out_edges)) {
                return 1;
            }
        }
    }
    /* else: no embedded package.tl — fall back to metadata depends[] if present */
    else if (archive.metadata.depends_count > 0) {
        for (u16 i = 0; i < archive.metadata.depends_count; i++) {
            str dep_name, dep_version;
            if (tl_tpkg_parse_dep_string(alloc, archive.metadata.depends[i], &dep_name, &dep_version)) {
                fprintf(stderr, "error: malformed dependency string in '%s': '%s'\n", str_cstr(&tpkg_name),
                        str_cstr(&archive.metadata.depends[i]));
                return 1;
            }
            if (resolve_dep(alloc, dep_name, dep_version, str_empty(), name, version, pkg_info, opts,
                            out_deps, out_edges)) {
                return 1;
            }
        }
    }

    return 0;
}

static int cmp_locked_dep(void const *a, void const *b) {
    tl_locked_dep const *da = a;
    tl_locked_dep const *db = b;
    int                  c  = str_cmp(da->name, db->name);
    if (c != 0) return c;
    return str_cmp(da->version, db->version);
}

static int cmp_lock_edge(void const *a, void const *b) {
    tl_lock_edge const *ea = a;
    tl_lock_edge const *eb = b;
    int                 c  = str_cmp(ea->name, eb->name);
    if (c != 0) return c;
    c = str_cmp(ea->version, eb->version);
    if (c != 0) return c;
    c = str_cmp(ea->dep_name, eb->dep_name);
    if (c != 0) return c;
    return str_cmp(ea->dep_version, eb->dep_version);
}

static tl_locked_dep *find_locked(tl_lockfile *lockfile, str name, str version) {
    for (u32 i = 0; i < lockfile->dep_count; i++) {
        if (str_eq(lockfile->deps[i].name, name) && str_eq(lockfile->deps[i].version, version)) {
            return &lockfile->deps[i];
        }
    }
    return null;
}

int tl_fetch(allocator *alloc, tl_fetch_opts const *opts) {
    tl_package pkg = {0};
    if (tl_package_parse_file(alloc, opts->package_tl_path, &pkg)) {
        return 1;
    }

    if (pkg.dep_count == 0) {
        if (opts->verbose) fprintf(stderr, "No dependencies declared in package.tl\n");
        return 0;
    }

    // Ensure at least one depend_path exists for saving
    if (pkg.info.depend_path_count == 0) {
        fprintf(stderr, "error: package.tl must declare at least one depend_path() for fetch\n");
        return 1;
    }

    // Try to parse existing lock file
    str         lock_path_str = str_init_static(opts->lock_path);
    tl_lockfile lockfile      = {0};
    int         have_lock =
      file_exists(lock_path_str) && tl_lockfile_parse_file(alloc, opts->lock_path, &lockfile) == 0;

    // Check if package.tl matches the lock file
    int lock_matches = 1;
    if (have_lock) {
        for (u32 i = 0; i < pkg.dep_count; i++) {
            if (!find_locked(&lockfile, pkg.deps[i].name, pkg.deps[i].version)) {
                lock_matches = 0;
                break;
            }
        }
    }

    // Case: Lock file exists and matches — verify/download
    if (have_lock && lock_matches) {
        if (opts->verbose) fprintf(stderr, "Lock file matches package.tl, verifying...\n");

        int fetched = 0, verified = 0, failed = 0;

        for (u32 i = 0; i < lockfile.dep_count; i++) {
            tl_locked_dep *dep       = &lockfile.deps[i];
            str            tpkg_name = tl_tpkg_filename(alloc, dep->name, dep->version);

            // Try to find the file locally
            str local_path = str_empty();
            for (u32 j = 0; j < pkg.info.depend_path_count; j++) {
                str dp        = resolve_dep_path(alloc, opts->work_dir, pkg.info.depend_paths[j]);
                str candidate = str_cat_3(alloc, dp, S("/"), tpkg_name);
                if (file_exists(candidate)) {
                    local_path = candidate;
                    break;
                }
            }

            if (!str_is_empty(local_path)) {
                // Verify hash
                char *fdata;
                u32   fsize;
                file_read(alloc, str_cstr(&local_path), &fdata, &fsize);
                if (fdata) {
                    str file_hash = compute_hash(alloc, fdata, fsize);
                    if (str_eq(file_hash, dep->hash)) {
                        verified++;
                        continue;
                    }
                    fprintf(stderr, "warning: hash mismatch for '%s', re-downloading\n",
                            str_cstr(&tpkg_name));
                }
            }

            // Download
            if (str_is_empty(dep->base_url)) {
                fprintf(stderr, "error: '%s' not found locally and no URL in lock file\n",
                        str_cstr(&tpkg_name));
                failed++;
                continue;
            }

            str full_url = build_download_url(alloc, dep->base_url, dep->name, dep->version);
            if (opts->verbose) fprintf(stderr, "Fetching: %s\n", str_cstr(&full_url));

            char *ddata;
            u32   dsize;
            file_url_get_ext(alloc, str_cstr(&full_url), &ddata, &dsize, opts->url_opts);
            if (!ddata) {
                fprintf(stderr, "error: failed to download '%s'\n", str_cstr(&full_url));
                failed++;
                continue;
            }

            // Verify hash
            str dl_hash = compute_hash(alloc, ddata, dsize);
            if (!str_eq(dl_hash, dep->hash)) {
                fprintf(stderr, "error: hash mismatch for '%s' (download corrupted or tampered)\n",
                        str_cstr(&tpkg_name));
                fprintf(stderr, "  expected: %s\n  got:      %s\n", str_cstr(&dep->hash),
                        str_cstr(&dl_hash));
                failed++;
                continue;
            }

            // Save
            str dp        = resolve_dep_path(alloc, opts->work_dir, pkg.info.depend_paths[0]);
            str save_path = str_cat_3(alloc, dp, S("/"), tpkg_name);
            if (file_write(str_cstr(&save_path), ddata, dsize)) {
                fprintf(stderr, "error: failed to save '%s'\n", str_cstr(&save_path));
                failed++;
                continue;
            }
            fetched++;
        }

        fprintf(stderr, "fetch: %d verified, %d fetched", verified, fetched);
        if (failed > 0) fprintf(stderr, ", %d failed", failed);
        fprintf(stderr, "\n");

        return failed > 0 ? 1 : 0;
    }

    // Case: No lock file, or lock file out of date — resolve from scratch
    if (opts->verbose) {
        if (!have_lock) fprintf(stderr, "No lock file found, resolving dependencies...\n");
        else fprintf(stderr, "Lock file out of date, re-resolving dependencies...\n");
    }

    // Create depend_path directory if it doesn't exist
    {
        str dp = resolve_dep_path(alloc, opts->work_dir, pkg.info.depend_paths[0]);
        if (!file_exists(dp)) {
            platform_mkdir(str_cstr(&dp));
        }
    }

    locked_dep_array resolved_deps  = {.alloc = alloc};
    lock_edge_array  resolved_edges = {.alloc = alloc};

    // Resolve each direct dependency
    for (u32 i = 0; i < pkg.dep_count; i++) {
        tl_package_dep *dep = &pkg.deps[i];
        str             url = dep->url;

        // If no URL in package.tl but lock file has one, reuse it
        if (str_is_empty(url) && have_lock) {
            tl_locked_dep *prev = find_locked(&lockfile, dep->name, dep->version);
            if (prev && !str_is_empty(prev->base_url)) {
                url = prev->base_url;
            }
        }

        if (resolve_dep(alloc, dep->name, dep->version, url, str_empty(), str_empty(), &pkg.info, opts,
                        &resolved_deps, &resolved_edges)) {
            return 1;
        }
    }

    // Sort for deterministic output
    if (resolved_deps.size > 1) {
        qsort(resolved_deps.v, resolved_deps.size, sizeof(tl_locked_dep), cmp_locked_dep);
    }
    if (resolved_edges.size > 1) {
        qsort(resolved_edges.v, resolved_edges.size, sizeof(tl_lock_edge), cmp_lock_edge);
    }

    // Write lock file
    if (tl_lockfile_write(opts->lock_path, resolved_deps.v, resolved_deps.size, resolved_edges.v,
                          resolved_edges.size)) {
        fprintf(stderr, "error: failed to write package.tl.lock\n");
        return 1;
    }

    fprintf(stderr, "fetch: resolved %u dependencies, wrote package.tl.lock\n", resolved_deps.size);
    return 0;
}
