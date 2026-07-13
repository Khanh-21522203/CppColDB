# State Machines for AI Systems: Working Idea for Repo-Specific Agents

## Purpose of this note

This document summarizes the main idea from our discussion about using deterministic state machines together with LLMs. It is intended to be used as a briefing document for a coding agent working inside a concrete repository.

The goal is not to build a generic coding agent from scratch. Tools like Claude Code, Codex CLI, Cursor, and similar agents already do a good job at generic tasks such as reading code, planning, editing files, running tests, and fixing failures.

The more valuable idea is different:

> Do not model how to code in general. Model how this specific repository must be changed safely.

That means extracting repo-specific workflows, invariants, owner preferences, change-risk categories, and validation gates, then expressing those as explicit deterministic workflows or state machines.

---

## Core thesis

Modern AI systems should avoid putting all workflow logic inside the LLM or an agent prompt.

A safer architecture is:

```text
Deterministic core + LLM at the edges
```

Or, more concretely:

```text
Program controls workflow.
LLM handles fuzzy judgment.
Real tools/APIs execute only when the deterministic workflow allows it.
```

This is the inversion of the common agent pattern:

```text
Risky pattern:
LLM/agent is the controller -> it decides what tool to call and when

Preferred pattern:
Program/state machine is the controller -> it calls the LLM only at specific states
```

The LLM should not be the authority for critical workflow transitions. The LLM can help interpret, summarize, draft, classify, or propose, but the deterministic program should decide what is allowed next.

---

## Determinism in this context

Determinism means:

```text
Given the same input and state, the workflow transition is predictable.
```

Non-determinism means:

```text
Given the same input, the output may vary or require judgment.
```

LLMs are useful for non-deterministic or fuzzy tasks, such as:

- interpreting ambiguous user intent
- checking whether information is missing
- drafting an email
- rewriting tone
- summarizing code
- proposing a patch
- classifying a change type
- explaining a test failure

State machines are useful for deterministic control flow, such as:

- what state the system is currently in
- what events are accepted in that state
- what transitions are valid
- what validations are required before moving forward
- what actions require human approval
- when a side effect is allowed
- when a retry, rollback, or failure path is required

---

## The email demo pattern

The discussed video demo used a state machine for an AI email-drafting workflow.

A simplified version:

```text
collect_requirements
  -> evaluate_requirements
  -> ask_for_missing_info, if information is missing
  -> draft_email, if requirements are complete
  -> review_draft
  -> revise_draft, if user requests changes
  -> approved, if user approves
  -> send_email
  -> sent
```

Important point:

The user can chat with the workflow, but the user message does not go directly to an unconstrained LLM controller.

Instead:

```text
User message
  -> app receives message
  -> state machine checks current state
  -> message is interpreted according to that state
  -> LLM is called only if that state needs fuzzy processing
  -> state machine decides the next transition
```

For example:

```text
If current state = collecting_requirements:
  User message means: add or update requirements.

If current state = reviewing_draft:
  User message may mean: approve, reject, or request revision.

If current state = approved:
  User message or button may trigger: send.
```

The same user message can mean different things depending on the current state. That is a key benefit of explicit state.

---

## Human approval is optional per transition

A state machine does not mean every transition requires a human.

Some transitions can be automatic:

```text
collecting_requirements -> evaluating_requirements

evaluating_requirements -> drafting, if required information is complete

drafting -> reviewing, after the LLM returns a draft

reviewing -> drafting, if the user requests changes
```

Some transitions should be human-gated:

```text
reviewing -> approved
approved -> sending
```

General rule:

```text
Low-risk, reversible, internal steps can be automatic.
High-risk, external, irreversible, or user-visible side effects should be approval-gated.
```

Examples of high-risk side effects:

- sending an email
- placing a trade
- deploying to production
- deleting data
- changing permissions
- running a migration
- transferring funds
- exposing an endpoint

---

## XState versus the general pattern

XState is mainly a JavaScript/TypeScript state machine and statechart library. It is a strong implementation option if the project is in JS/TS.

But the architecture is language-agnostic.

Equivalent approaches can be implemented with:

- XState in JavaScript/TypeScript
- `transitions` or `python-statemachine` in Python
- Stateless in C#/.NET
- Spring Statemachine in Java
- FSM libraries in Go or Rust
- Temporal for long-running deterministic workflows
- a custom workflow runner if the workflow is simple

The important concept is not XState itself. The important concept is:

```text
Make the workflow explicit, inspectable, testable, and constrained.
```

---

## LLM API versus LLM CLI

There are two different kinds of CLI that should not be confused.

### 1. Existing AI coding CLI

Examples:

- Codex CLI
- Claude Code
- Cursor-style coding agents

These are useful as developer tools. They can help write code, inspect files, refactor, run commands, and generate tests.

They should not usually be the runtime controller for a production AI workflow.

### 2. A CLI built for your own app

You may build your own CLI, such as:

```bash
my-email-agent start
my-email-agent input "email John about my talk"
my-email-agent approve
my-email-agent send
```

This CLI is just an interface. The core workflow still lives in your application code.

Runtime architecture should usually look like:

```text
Web app / backend / CLI / Slack bot
  -> deterministic workflow or state machine
  -> LLM API calls at specific states
  -> validation and guard checks
  -> real external APIs only after valid transitions
```

So the runtime should generally call Claude API, OpenAI API, or another LLM API directly, rather than shelling out to a coding CLI as the main controller.

---

## Yes, this means managing context and session explicitly

When not using an existing CLI as the runtime controller, the application needs to manage:

- session state
- conversation history
- current workflow state
- workflow context
- LLM prompt construction
- tool results
- retry behavior
- approval state
- audit logs
- validation results
- side-effect permissions

This is extra work, but it is also the source of safety and control.

A production system should know things like:

```text
What is the current state?
What information was user-provided?
What information was inferred by the LLM?
What tool results were observed?
What output was approved by the user?
What side effect is now allowed?
What should be logged for audit?
```

The benefit is:

```text
Control, safety, observability, testability, and debuggability.
```

---

## Applying the idea to a repo or project

The email demo is a business workflow.

For a software repository, the equivalent is an engineering workflow.

However, generic coding workflows are often not valuable enough to model, because existing coding agents already handle them well.

Generic workflow:

```text
understand_request
  -> inspect_codebase
  -> propose_plan
  -> edit_code
  -> run_tests
  -> fix_failures
  -> summarize_changes
```

This is useful, but not the main opportunity.

The more valuable target is repo-specific workflow:

```text
classify_change_surface
  -> load_repo_contracts
  -> derive_required_checks
  -> propose_repo_specific_plan
  -> execute_change
  -> run_required_checks
  -> invariant_review
  -> final_summary
```

This is different because it captures what this repository specifically cares about.

---

## Do not create one huge state machine for the whole repo

A repository does not necessarily need one giant state machine.

Better rule:

```text
Each confusing, risky, multi-step workflow deserves an explicit model.
```

A repo may have:

- zero state machines
- one main state machine
- several smaller state machines
- one high-level workflow plus child workflows
- a structured change-surface matrix instead of a full state machine

Do not model everything. Model the parts that are confusing, risky, repeated, or easy for an agent to get wrong.

---

## When a state machine is worth using

A state machine or explicit workflow is useful when there are:

- multiple phases with valid/invalid orderings
- important intermediate states
- side effects
- approval gates
- retries
- rollbacks
- timeouts
- external systems
- compatibility concerns
- safety constraints
- domain invariants
- review rules that agents often forget

It may be overkill for simple tasks like:

- explaining a function
- renaming a variable
- writing a small unit test
- formatting a file
- summarizing a document

---

## Repo-specific state machines should model safe change, not generic coding

The agent should not just model this:

```text
inspect -> edit -> test
```

Instead, it should model this:

```text
How must this repo be changed safely?
```

Useful repo-specific states might include:

```text
identify_repo_contracts
locate_ownership_boundaries
classify_change_surface
check_public_api_impact
check_protocol_impact
check_schema_impact
check_runtime_lifecycle_impact
check_deployment_impact
derive_required_validations
apply_repo_pattern
run_repo_specific_checks
verify_no_contract_break
summarize_risk_and_validation
```

The purpose is to encode repository protocol, not coding ability.

---

## Change surface classification

A useful repo-aware workflow starts by classifying the change surface.

Possible categories:

```text
docs_only
test_only
pure_refactor
public_api_change
wire_protocol_change
db_schema_change
runtime_lifecycle_change
deployment_change
security_sensitive_change
performance_sensitive_change
configuration_change
observability_change
concurrency_change
```

Each category should map to different risks, checks, and approval requirements.

Example:

```yaml
change_surfaces:
  docs_only:
    risk: low
    approval: not_required
    required_checks:
      - terminology_matches_repo

  pure_refactor:
    risk: medium
    approval: optional
    required_checks:
      - no_public_api_change
      - no_behavior_change_expected
      - relevant_tests_pass

  public_api_change:
    risk: high
    approval: required
    required_checks:
      - backward_compatibility_review
      - docs_updated
      - tests_updated
      - migration_note_if_needed

  deployment_change:
    risk: high
    approval: required
    required_checks:
      - rollback_path_checked
      - health_readiness_checked
      - config_impact_reviewed
      - dry_run_if_available
```

---

## Owner style should become explicit rules

A key idea is to extract the repository owner's review preferences and turn them into rules or workflow gates.

Owner style often lives in:

- PR comments
- code review feedback
- commit history
- issue discussions
- README and CONTRIBUTING files
- test naming patterns
- CI configuration
- existing examples
- previously approved changes
- previously rejected changes

Examples of owner feedback becoming deterministic rules:

```text
Owner comment:
"Do not change this wire format without examples."

Workflow rule:
If change_surface = wire_protocol_change:
  require example update
  require parser/encoder tests
  require compatibility review
```

```text
Owner comment:
"Do not expose admin endpoints outside localhost."

Workflow rule:
If change_surface = runtime_endpoint_change:
  require loopback/security guard verification
  require security review
```

```text
Owner comment:
"Do not rely on metadata alone; check runtime state too."

Workflow rule:
If change_surface = deployment_or_runtime_state_change:
  require metadata check
  require actual runtime state check
  require reconciliation step
```

This is where the state machine becomes repo-specific and valuable.

---

## Suggested artifacts to generate for an existing repo

Before generating any state machine, ask the agent to produce these artifacts:

### 1. Repo Map

A concise map of:

- main modules
- ownership boundaries
- entrypoints
- external integrations
- public contracts
- side-effecting components
- tests and validation commands

### 2. Contract and Invariant List

Things that must not be accidentally broken:

- public APIs
- wire formats
- database schemas
- config semantics
- deployment behavior
- security boundaries
- lifecycle rules
- backward compatibility constraints
- performance-sensitive paths

### 3. Change Surface Matrix

A matrix mapping:

```text
change type -> risk level -> required checks -> approval requirement
```

### 4. Owner Rules

A structured list of inferred owner preferences:

```yaml
owner_rules:
  - id: preserve_wire_compatibility
    applies_to: wire_protocol_change
    rule: Do not change wire behavior without examples and compatibility tests.
    source: inferred_from_tests_and_existing_patterns

  - id: no_unreviewed_deploy_side_effects
    applies_to: deployment_change
    rule: Deployment behavior changes require rollback and readiness validation.
    source: inferred_from_ci_and_deploy_scripts
```

### 5. Repo-Specific Workflow Proposal

A proposed state machine or workflow derived from the artifacts above.

---

## Proposed high-level workflow for repo-aware agent

This is a good starting point:

```text
intake_change
  -> classify_change_surface
  -> load_repo_model
  -> derive_required_checks
  -> propose_repo_specific_plan
  -> owner_gate_if_high_risk
  -> execute_change
  -> run_required_checks
  -> invariant_review
  -> final_summary
```

Expanded version:

```text
intake_change
  - receive user request
  - capture constraints
  - identify explicit success criteria

classify_change_surface
  - classify the request into one or more repo-specific change surfaces
  - estimate risk
  - detect whether public contracts may be affected

load_repo_model
  - load repo map
  - load owner rules
  - load change-surface matrix
  - load relevant module patterns

derive_required_checks
  - select required tests
  - select required review gates
  - select compatibility checks
  - select documentation or example updates

propose_repo_specific_plan
  - produce a plan tied to repo contracts and owner rules
  - avoid generic coding steps unless connected to a repo-specific requirement

owner_gate_if_high_risk
  - ask for approval if the change affects public API, deployment, schema, security, protocol, or irreversible side effects

execute_change
  - allow edits only within the declared scope unless the state machine expands scope explicitly

run_required_checks
  - run the validations derived earlier
  - do not substitute unrelated tests for required checks

invariant_review
  - check whether any repo contract or owner rule may have been violated
  - if violated, loop back to execute_change or request human input

final_summary
  - summarize changed files
  - summarize risk classification
  - summarize validations run
  - summarize unvalidated assumptions
```

---

## Example of a repo-specific workflow: protocol change

```text
intake_change
  -> classify_as_protocol_change
  -> locate_protocol_contracts
  -> inspect_parser_encoder_examples
  -> check_backward_compatibility
  -> plan_protocol_update
  -> require_owner_approval
  -> edit_protocol_code
  -> update_examples
  -> update_tests
  -> run_protocol_tests
  -> protocol_invariant_review
  -> final_summary
```

Useful checks:

```text
- Did the public/wire behavior change?
- Are existing clients still compatible?
- Were examples updated?
- Were parser and encoder tests updated?
- Was any schema or tag meaning changed?
- Is the migration impact documented?
```

---

## Example of a repo-specific workflow: deployment change

```text
intake_change
  -> classify_as_deployment_change
  -> locate_deploy_scripts_and_configs
  -> identify_runtime_state_sources
  -> check_health_and_readiness_model
  -> check_rollback_path
  -> plan_deployment_change
  -> require_owner_approval
  -> edit_deployment_code
  -> run_dry_run_or_static_validation
  -> verify_no_accidental_downgrade
  -> final_summary
```

Useful checks:

```text
- What is the source of truth for runtime state?
- Can this cause accidental downgrade?
- Is readiness checked before promotion?
- Is rollback possible?
- Are old and new versions handled safely?
- Are config changes backward-compatible?
```

---

## Example of a repo-specific workflow: runtime lifecycle change

```text
intake_change
  -> classify_as_lifecycle_change
  -> locate_startup_shutdown_paths
  -> inspect_cancellation_and_cleanup
  -> check_concurrency_risks
  -> check_side_effect_boundaries
  -> plan_lifecycle_change
  -> edit_code
  -> run_lifecycle_tests
  -> review_race_and_cleanup_paths
  -> final_summary
```

Useful checks:

```text
- Are startup and shutdown still ordered correctly?
- Are cancellation paths clean?
- Are resources closed?
- Are race conditions introduced?
- Are background tasks supervised?
- Are readiness and liveness semantics preserved?
```

---

## How to use an LLM inside this workflow

The state machine can call the LLM in fuzzy states, for example:

```text
classify_change_surface:
  Ask LLM to classify the request based on repo model.

derive_required_checks:
  Ask LLM to propose checks, then validate against structured rules.

propose_repo_specific_plan:
  Ask LLM to produce a plan tied to repo-specific invariants.

invariant_review:
  Ask LLM to inspect the diff against owner rules and contracts.
```

But the LLM should not have final authority over:

```text
- whether a high-risk change is safe
- whether approval is required
- whether tests can be skipped
- whether a side effect can run
- whether a public contract may change silently
```

Those should be determined by the workflow rules.

---

## Anti-patterns to avoid

### Anti-pattern 1: Natural-language control flow only

Bad:

```text
"Always run tests. Never deploy without approval. Be careful with public APIs."
```

This can be useful context, but it is weak as the only enforcement mechanism.

Better:

```yaml
if_change_surface: public_api_change
approval: required
required_checks:
  - backward_compatibility_review
  - docs_updated
  - tests_updated
```

### Anti-pattern 2: One giant repo state machine

Bad:

```text
Model every possible action in the entire repository as one huge state machine.
```

Better:

```text
Create small workflows for confusing or risky change types.
```

### Anti-pattern 3: Rebuilding a generic coding agent

Bad:

```text
Build a state machine that only says inspect -> plan -> edit -> test.
```

Better:

```text
Build a workflow that encodes repo-specific contracts, owner rules, and required checks.
```

### Anti-pattern 4: Letting the LLM decide side effects

Bad:

```text
LLM decides when to send email, deploy, delete data, or place order.
```

Better:

```text
State machine decides whether the transition is valid.
LLM may only prepare or recommend.
```

---

## Prompt to give to a coding agent in a concrete repo

Use this prompt when starting with an existing repository:

```text
Read this repository, but do not edit files yet.

Your job is to infer a repo-specific deterministic workflow model for safe AI-assisted changes.

Do not produce a generic coding workflow like "inspect, plan, edit, test" unless each step is tied to repo-specific contracts or owner rules.

Produce the following artifacts:

1. Repo Map
   - main modules
   - ownership boundaries
   - entrypoints
   - external integrations
   - public contracts
   - side-effecting components
   - test and validation commands

2. Contract and Invariant List
   - public APIs
   - wire formats
   - schemas
   - config semantics
   - deployment behavior
   - lifecycle rules
   - security boundaries
   - backward compatibility constraints

3. Change Surface Matrix
   For each change surface, provide:
   - description
   - risk level
   - approval requirement
   - required checks
   - likely affected files/modules

4. Owner Rules
   Infer rules from README, CONTRIBUTING, tests, CI, examples, code patterns, commit history, and PR review comments if available.

5. Proposed State Machines or Workflows
   Propose small workflows for the highest-risk or most confusing change surfaces.

6. Recommended Storage Format
   Suggest how to store this model in the repo, for example:
   - .repo-model/repo-map.md
   - .repo-model/change-surfaces.yaml
   - .repo-model/owner-rules.md
   - .repo-model/workflows/*.machine.json
   - AGENTS.md with structured sections

Important constraints:
- Do not edit code yet.
- Prefer repo-specific rules over generic best practices.
- Mark uncertain inferences clearly.
- Distinguish facts found in the repo from guesses.
- Recommend human approval gates for high-risk transitions.
```

---

## Possible repository structure for the model

A lightweight option:

```text
AGENTS.md
repo-workflows.md
repo-rules.yaml
```

A more structured option:

```text
.repo-model/
  repo-map.md
  contracts.md
  owner-rules.md
  change-surfaces.yaml
  workflows/
    safe-change.machine.json
    protocol-change.machine.json
    deployment-change.machine.json
```

The structured option is better if a workflow runner or state machine engine will consume the model.

---

## Minimal YAML example

```yaml
change_surfaces:
  docs_only:
    risk: low
    approval: not_required
    required_checks:
      - terminology_matches_repo

  pure_refactor:
    risk: medium
    approval: optional
    required_checks:
      - no_public_api_change
      - no_behavior_change_expected
      - relevant_tests_pass

  public_api_change:
    risk: high
    approval: required
    required_checks:
      - backward_compatibility_review
      - docs_updated
      - tests_updated
      - migration_note_if_needed

  deployment_change:
    risk: high
    approval: required
    required_checks:
      - rollback_path_checked
      - health_readiness_checked
      - config_impact_reviewed
      - dry_run_if_available

owner_rules:
  - id: preserve_public_contracts
    applies_to:
      - public_api_change
      - wire_protocol_change
      - db_schema_change
    rule: Public contracts must not change silently.
    enforcement: require explicit compatibility review and summary.

  - id: approval_for_high_risk_changes
    applies_to:
      - deployment_change
      - security_sensitive_change
      - db_schema_change
      - wire_protocol_change
    rule: High-risk changes require human approval before execution.
    enforcement: block execute_change until approved.
```

---

## Minimal state machine sketch

```json
{
  "id": "repoSafeChangeWorkflow",
  "initial": "intake_change",
  "states": {
    "intake_change": {
      "on": {
        "REQUEST_RECEIVED": "classify_change_surface"
      }
    },
    "classify_change_surface": {
      "on": {
        "CLASSIFIED_LOW_RISK": "derive_required_checks",
        "CLASSIFIED_HIGH_RISK": "load_owner_rules"
      }
    },
    "load_owner_rules": {
      "on": {
        "RULES_LOADED": "derive_required_checks"
      }
    },
    "derive_required_checks": {
      "on": {
        "CHECKS_DERIVED": "propose_repo_specific_plan"
      }
    },
    "propose_repo_specific_plan": {
      "on": {
        "LOW_RISK_PLAN_READY": "execute_change",
        "HIGH_RISK_PLAN_READY": "await_human_approval"
      }
    },
    "await_human_approval": {
      "on": {
        "APPROVED": "execute_change",
        "REJECTED": "final_summary"
      }
    },
    "execute_change": {
      "on": {
        "CHANGE_APPLIED": "run_required_checks"
      }
    },
    "run_required_checks": {
      "on": {
        "CHECKS_PASSED": "invariant_review",
        "CHECKS_FAILED": "repair_change"
      }
    },
    "repair_change": {
      "on": {
        "REPAIR_APPLIED": "run_required_checks",
        "NEEDS_HUMAN_INPUT": "await_human_approval"
      }
    },
    "invariant_review": {
      "on": {
        "NO_VIOLATIONS_FOUND": "final_summary",
        "VIOLATION_FOUND": "repair_change"
      }
    },
    "final_summary": {
      "type": "final"
    }
  }
}
```

This is only a starting sketch. The real value comes from replacing generic states and events with repo-specific surfaces, checks, and owner rules.

---

## Final takeaway

The key idea is not:

```text
Use a state machine for everything.
```

The key idea is:

```text
Use explicit deterministic models for the parts of a system or repo where uncontrolled LLM behavior is risky, confusing, or hard to review.
```

For an existing repo, the best model should be extracted from:

```text
repo structure
existing code patterns
tests
CI
contracts
deployment scripts
review comments
owner preferences
known failure modes
```

The goal is to make the repository's implicit rules explicit enough that an AI agent can follow them reliably.

In one sentence:

> Build the workflow that captures how this repo must be changed safely, then let the LLM operate inside that workflow instead of letting it invent the workflow on the fly.
