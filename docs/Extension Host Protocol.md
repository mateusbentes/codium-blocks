# Extension Host Protocol

This file is a compatibility pointer retained for links from earlier development snapshots. The **canonical Extension Host protocol is version 2** and is maintained in [`protocol/README.md`](../protocol/README.md).

Version 1 described the initial prototype and is not the current contract. New integrations must implement protocol 2 and negotiate the `protocol` value in the `ready` message. Historical protocol 1 consumers are not promised compatibility unless a future adapter explicitly documents it.

The canonical document defines JSON Lines transport, host and broker messages, extension contributions, persistent configuration, workspace document events, and the language-server process manager. Changes to the wire contract must update the canonical document, the host smoke test, and the protocol compatibility matrix together.
