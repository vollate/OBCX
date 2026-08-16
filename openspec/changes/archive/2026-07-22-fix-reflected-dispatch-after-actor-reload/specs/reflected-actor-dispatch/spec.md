## ADDED Requirements

### Requirement: Reflected dispatch remains local to its actor generation

Generated reflected dispatch SHALL resolve and invoke handlers using code and
actor-specific metadata owned by the currently executing actor library
generation. It MUST NOT retain a process-unique or cross-generation static
dispatch table containing canonical-name views, actor dispatch function
pointers, or other references whose lifetime belongs to a separately staged
copy of the actor DSO.

#### Scenario: The same actor is reloaded after dispatch was initialized

- **WHEN** one staged actor generation handles a supported canonical message and later reloads stage new copies of the same actor DSO
- **THEN** every new generation recognizes that canonical message and invokes the handler compiled into its own loaded image

#### Scenario: A retired generation is released

- **WHEN** the last route using a retired actor generation completes and its DSO becomes eligible for unload
- **THEN** no reflected dispatch state used by an active generation references the retired generation's canonical-name views or handler functions

#### Scenario: Current generation receives an unsupported type

- **WHEN** the active actor generation receives a canonical input that none of its reflected handlers accept
- **THEN** dispatch returns `unsupported_message_type` without consulting metadata from any earlier generation
