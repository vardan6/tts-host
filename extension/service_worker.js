const DEFAULT_PORT = 7861;

chrome.action.onClicked.addListener(async (tab) => {
  if (!tab.id) return;
  try {
    await chrome.scripting.executeScript({ target: { tabId: tab.id }, files: ["content.js"] });
  } catch (error) {
    showResult(false, "This page does not allow selection capture.");
  }
});

chrome.runtime.onMessage.addListener((message, sender) => {
  if (message?.type !== "tts-host-selection" || !sender.tab) return;
  void sendSelection(message.text);
});

async function sendSelection(value) {
  const text = typeof value === "string" ? value : "";
  if (!text.trim()) {
    showResult(false, "Select page text first.");
    return;
  }
  const { port = DEFAULT_PORT } = await chrome.storage.sync.get({ port: DEFAULT_PORT });
  try {
    const response = await fetch(`http://127.0.0.1:${port}/v1/selection`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ text })
    });
    if (!response.ok) {
      const detail = await response.text();
      throw new Error(detail || `Host returned HTTP ${response.status}`);
    }
    showResult(true, "Selection sent to TTS Host.");
  } catch (error) {
    showResult(false, `Could not reach TTS Host on port ${port}. Check that it is running.`);
  }
}

function showResult(success, title) {
  chrome.action.setBadgeText({ text: success ? "OK" : "!" });
  chrome.action.setBadgeBackgroundColor({ color: success ? "#287a3d" : "#a43b32" });
  chrome.action.setTitle({ title });
  setTimeout(() => chrome.action.setBadgeText({ text: "" }), 3000);
}
