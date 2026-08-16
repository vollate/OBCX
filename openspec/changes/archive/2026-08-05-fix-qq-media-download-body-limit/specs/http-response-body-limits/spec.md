## ADDED Requirements

### Requirement: HTTP responses have a finite default body limit
OBCX `HttpClient` SHALL apply an 8 MiB response-body limit when a caller does not configure another limit. The limit MUST apply without disabling normal timeout, TLS, proxy, header, or decompression behavior.

#### Scenario: Default client receives a response within the limit
- **WHEN** an HTTP response body is no larger than 8 MiB and the response is otherwise valid
- **THEN** the client completes the response read normally

#### Scenario: Default client receives a response above the limit
- **WHEN** an HTTP response declares or streams a body larger than 8 MiB
- **THEN** the client aborts the response read with `HttpClientError` instead of buffering the complete body

### Requirement: Callers can select a positive per-client body limit
OBCX `HttpClient` SHALL allow a caller to set a finite positive response-body limit before issuing a request. The selected limit MUST remain scoped to that client instance and MUST NOT change the default or another client instance.

#### Scenario: Media client opts into a larger finite limit
- **WHEN** a caller configures a 10 MiB limit and receives a valid 9 MiB response
- **THEN** that client accepts the response while an unconfigured client retains the 8 MiB default

#### Scenario: Response exceeds the selected limit
- **WHEN** a client configured for a finite limit receives a response one or more bytes above that limit
- **THEN** the client aborts the read with `HttpClientError`

#### Scenario: Caller selects an unlimited-equivalent value
- **WHEN** a caller attempts to configure a zero-byte response limit
- **THEN** the client rejects the setting and does not interpret it as unlimited

### Requirement: Response limits are consistent across transports and operations
The configured response-body limit SHALL govern every response-bearing asynchronous and synchronous direct or proxy HTTP operation. A transport or operation MUST NOT silently fall back to Beast's implicit limit or an unlimited parser.

#### Scenario: Direct and proxy GET responses use the selected limit
- **WHEN** equivalent direct and proxied GET responses exceed their clients' configured limit
- **THEN** both reads fail under the same response-limit policy

#### Scenario: POST response uses the selected limit
- **WHEN** a POST response exceeds the configured response-body limit
- **THEN** the response read fails even if the request body was accepted by the remote endpoint

#### Scenario: Chunked response crosses the limit
- **WHEN** a response has no known `Content-Length` and decoded body octets cross the selected parser limit
- **THEN** the client stops reading and reports `HttpClientError`
