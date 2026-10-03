const DEFAULT_PORT = 7861;
const READ_ARTICLE_MENU_ID = "tts-host-read-article";
const PLAYBACK_MENU_ID = "tts-host-playback";

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({
      id: READ_ARTICLE_MENU_ID,
      title: "Read article with TTS Host",
      contexts: ["page"]
    });
    chrome.contextMenus.create({
      id: PLAYBACK_MENU_ID,
      title: "TTS Host playback controls",
      contexts: ["page", "action"]
    });
  });
});

chrome.action.onClicked.addListener(async (tab) => {
  if (!tab.id) return;
  try {
    await chrome.scripting.executeScript({ target: { tabId: tab.id }, files: ["content.js"] });
  } catch (error) {
    showResult(false, "This page does not allow selection capture.");
  }
});

chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  if (info.menuItemId === PLAYBACK_MENU_ID) {
    await chrome.tabs.create({ url: chrome.runtime.getURL("playback.html") });
    return;
  }
  if (info.menuItemId !== READ_ARTICLE_MENU_ID || !tab?.id) return;
  try {
    await chrome.scripting.executeScript({ target: { tabId: tab.id }, files: ["article.js"] });
  } catch (error) {
    showResult(false, "This page does not allow article reading.");
  }
});

chrome.runtime.onMessage.addListener((message, sender) => {
  if (!sender.tab) return;
  if (message?.type === "tts-host-selection") {
    void sendText(message.text, "selection");
  } else if (message?.type === "tts-host-article") {
    void sendText(message.text, "article");
  }
});

async function sendText(value, source) {
  const text = typeof value === "string" ? value : "";
  if (!text.trim()) {
    showResult(false, source === "article" ? "No readable page text found." : "Select page text first.");
    return;
  }
  const body = JSON.stringify({ text });
  if (new TextEncoder().encode(body).length > 1024 * 1024) {
    showResult(false, "This page has more text than TTS Host accepts in one request.");
    return;
  }
  const { port = DEFAULT_PORT } = await chrome.storage.sync.get({ port: DEFAULT_PORT });
  try {
    const response = await fetch(`http://127.0.0.1:${port}/v1/selection`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body
    });
    if (!response.ok) {
      const detail = await response.text();
      showResult(false, `TTS Host rejected the ${source}: ${detail || `HTTP ${response.status}`}`);
      return;
    }
    showResult(true, source === "article" ? "Article sent to TTS Host." : "Selection sent to TTS Host.");
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
