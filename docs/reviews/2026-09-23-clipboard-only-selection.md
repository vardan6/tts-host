# Clipboard-only selection capture

## Finding

With `selection.capturePolicy` set to `clipboardOnly`, Ctrl+F8 reported that
capture was unavailable because no focused-control Copy adapter had passed
compatibility verification. Settings exposed the policy, but selecting it could
not provide a working path for applications whose UI Automation provider did
not expose the selected text.

## Analysis

The existing **Read clipboard** command already read text from the clipboard
without sending input. The `clipboardOnly` hotkey path instead rejected the
request while waiting for a safe synthetic Copy adapter. That behavior did not
match the user's desired manual-copy workflow.

## Resolution

`clipboardOnly` now reads the existing clipboard when Ctrl+F8 is pressed. The
user copies the intended selection first; this mode sends no keystrokes and can
read stale clipboard text if the user has not copied the current selection.
`automatic` remains the default and retains its direct-capture behavior.
Settings labels and product/design docs explain the distinction.

## Verification

The hotkey dispatch now calls the existing clipboard-read path for
`clipboardOnly`. Automated tests were not run. Native Windows verification is
still needed to confirm Ctrl+F8 speaks freshly copied text from the Codex
window and reports an actionable error for an empty clipboard.
