# ADR 0003 — No authentication on loopback; origin allowlist for browsers

- Status: accepted
- Date: 2026-08-27

## Decision

The local API requires no credential. It binds to `127.0.0.1` and any process on
the machine may call it.

`config.json` keeps an `authentication` key with defined values —
`"none" | "automatic" | "token"` — so the semantics exist before the schema is
frozen. Only `"none"` is implemented in the first release. `"none"` is legal
only while bound to loopback; binding to a non-loopback address without a token
is refused.

Browsers are the exception. The HTTP service sends
`Access-Control-Allow-Origin` for an explicit configured allowlist of origins —
the browser extension's `chrome-extension://<id>` — and never `*`.

## Why

Zero-setup is a product requirement: unzip, run, it works. A token file to
locate, copy, and paste into an extension is the kind of friction the product
exists to avoid, and it is what comparable local-inference tools omit for the
same reason.

The honest consequence: any local process can make the machine speak and can
enumerate installed models. On a single-user desktop that is an acceptable
trade. It is recorded here rather than left implicit.

CORS is the one place where "any local process" becomes "any web page you
happen to visit", and that is not an acceptable trade — a wildcard origin would
let an arbitrary site drive the service. The allowlist costs nothing, because
the extension's ID is fixed once published.

## Rejected

- **Auto-generated bearer token in an ACL-restricted file.** The stronger
  design, and the one to adopt if LAN binding ever becomes a real use case.
  Rejected for v1 on setup friction.
- **OS peer identity (named pipe / `SO_PEERCRED`).** Best local isolation, but
  it cannot serve a browser extension over HTTP, which is a required client.
- **Chrome Native Messaging instead of HTTP.** Removes the browser origin
  question entirely, but the extension would launch its own host instance,
  conflicting with the always-running background service.

## Revisit when

LAN binding is genuinely wanted, or a second untrusted local client appears.
The config key already reserves the vocabulary for that change.
