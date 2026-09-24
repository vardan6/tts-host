const selectedText = String(window.getSelection() ?? "");
chrome.runtime.sendMessage({ type: "tts-host-selection", text: selectedText });
