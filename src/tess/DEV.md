# TL Dev Notes


# Compiler

The compiler is responsible for converting a text program into
executable code. Currently, we accomplish this by type-checking the
program and producing simple C code.

The main work done by the compiler is type inferencing and synthesis
of generated types and functions to support the semantics of the TL
language.

The steps are as follows:

1. Parse the text input into a sequence of ast node trees. The top
   level is a sequence because of the existence of multiple top level
   expressions. For instance, each function definition is a single
   tree.

   The resulting ast is not necessarily a correct program.

2. Generate type constructors for user defined types.

3. Rename every variable in the program, respecting lexical scope.
   Ensuring that every symbol has a unique name makes certain things
   easier, and enforcing lexical scoping rules at the outset is also
   helpful.

   When we get to generic function specialisation, we have to repeat
   this step because the invariant that every occurence of a symbol
   has the same type would otherwise be invalidated.

4. Assign type variables to every symbol in the program, where
   'symbol' is understood to be any ast node representing a name that
   may be used by the program to refer to a specific object. For
   example, let bindings, parameter names, etc. By contrast, some
   symbols such as labels in a labelled tuple or field names in a user
   type do not hold their own types.

FIXME still not accurate

5. Synthesize types for every distinct tuple type in the program.
   These must be globally registered so that each occurence of the
   same-typed tuple is mapped to the same C type.

   A key question here is whether labelled tuples and unlabelled
   tuples are distinct and not compatible. Or are they distinct but
   with limited compatibility.

6. Synthesize types for every user defined type in the program. These
   are globally registered under the unmangled name.




7. Without considering function applications, collect constraints from
   the program by analysing the ast. For example, the type of the
   return value from the invocation of a function must match the type
   on the right hand side of the arrow type of the function. Both
   operands of an arithmetic infix operation must be the same type.
   And so on.

   Function application constraints are not considered in this phase,
   because all functions in the program are potentially generic, aka
   polymorphic.

8. Examine each callsite of a named function application, and generate
   a specialised version of the named generic function, constrained to
   the argument types at the callsite. These specialised functions are
   added to the program, which is not yet fully analysed.

9. Now considering function applications, repeat the constraint
   collection and satisfaction process. A well-typed program will
   result in every callsite having a well-typed value.
