const portInput = document.querySelector("#port");
const status = document.querySelector("#status");

chrome.storage.sync.get({ port: 7861 }, ({ port }) => {
  portInput.value = port;
});

document.querySelector("#save").addEventListener("click", async () => {
  const port = Number(portInput.value);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    status.textContent = "Enter a port from 1 to 65535.";
    return;
  }
  await chrome.storage.sync.set({ port });
  status.textContent = "Saved.";
});
