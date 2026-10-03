(() => {
  const semanticText = [...document.querySelectorAll("article, main")]
    .map((element) => String(element.innerText ?? "").trim())
    .filter(Boolean)
    .sort((left, right) => right.length - left.length)[0];
  const text = semanticText || String(document.body?.innerText ?? "").trim();

  chrome.runtime.sendMessage({ type: "tts-host-article", text });
})();
