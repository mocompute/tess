const vscode = require('vscode');
const { execFile } = require('child_process');
const path = require('path');

function activate(context) {
    // Register document formatting provider
    const formatter = vscode.languages.registerDocumentFormattingEditProvider('tl', {
        provideDocumentFormattingEdits(document) {
            return formatDocument(document);
        }
    });
    context.subscriptions.push(formatter);

    // Register range formatting provider
    const rangeFormatter = vscode.languages.registerDocumentRangeFormattingEditProvider('tl', {
        provideDocumentRangeFormattingEdits(document, range) {
            return formatDocument(document, range);
        }
    });
    context.subscriptions.push(rangeFormatter);

    // Format on save if configured
    const onSave = vscode.workspace.onWillSaveTextDocument(event => {
        const config = vscode.workspace.getConfiguration('tl');
        if (config.get('formatOnSave') && event.document.languageId === 'tl') {
            event.waitUntil(formatDocument(event.document));
        }
    });
    context.subscriptions.push(onSave);
}

function formatDocument(document, range) {
    const config = vscode.workspace.getConfiguration('tl');
    const tessExe = config.get('tessExecutable', 'tess');
    const text = range
        ? document.getText(range)
        : document.getText();

    return new Promise((resolve, reject) => {
        const child = execFile(tessExe, ['fmt'], {
            timeout: 10000,
            maxBuffer: 10 * 1024 * 1024
        }, (error, stdout, stderr) => {
            if (error) {
                vscode.window.showErrorMessage(`tess fmt failed: ${stderr || error.message}`);
                resolve([]);
                return;
            }
            const fullRange = range || new vscode.Range(
                document.positionAt(0),
                document.positionAt(document.getText().length)
            );
            resolve([vscode.TextEdit.replace(fullRange, stdout)]);
        });
        child.stdin.write(text);
        child.stdin.end();
    });
}

function deactivate() {}

module.exports = { activate, deactivate };
