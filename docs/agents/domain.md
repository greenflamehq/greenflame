# Domain Docs

How engineering skills should consume this repository's domain documentation.

## Before exploring, read these

- `CONTEXT.md` at the repository root.
- Relevant ADRs under `docs/adr/`.

If these files do not exist, proceed silently. Domain-modeling workflows create them when terminology or decisions need to be recorded.

## File structure

This is a single-context repository:

```
/
├── CONTEXT.md
├── docs/adr/
└── src/
```

## Use the glossary's vocabulary

When output names a domain concept—in an issue title, refactoring proposal, hypothesis, or test name—use the term defined in `CONTEXT.md`.

If a needed concept is absent, reconsider whether the terminology belongs to the project or note the gap for domain modeling.

## Flag ADR conflicts

Explicitly identify output that contradicts an existing ADR rather than silently overriding it.
