// websocket holds connection to the server
var socket = null;
var nestedContainer = null;

function wsConnect() {
    let protocol = window.location.protocol === "https:" ? "wss://" : "ws://";
    let wsUrl = protocol + window.location.host + ":1337";
    let accumulatedData = "";

    function connect() {
        socket = new WebSocket(wsUrl);

        socket.onopen = () => {
            document.getElementById("loading").innerText = "Connected to WebSocket server...\n";
        };

        socket.onclose = () => {
            document.getElementById("loading").innerText = "Disconnected from WebSocket server...\nReconnecting...";
            setTimeout(connect, 3000); // Reconnect after 3 seconds
        };

        socket.onmessage = (event) => {
            accumulatedData += event.data; // Append new chunk of data
            // Use a temporary variable to work with accumulatedData
            let tempData = accumulatedData; // Temporary copy of accumulatedData

            try {
                const jsonData = JSON.parse(tempData); // Attempt to parse tempData

                document.getElementById("loading").innerText = `Data Length: ${humanReadableSize(tempData.length)}\n`;
                renderList(jsonData); // Render the list dynamically
                // If successful, reset accumulatedData
                accumulatedData = ""; // Clear accumulatedData after rendering

            } catch (error) {
                // In case of parsing error, check for missing braces and fix in tempData
                const openBraces = (tempData.match(/{/g) || []).length; // Count opening braces
                const closeBraces = (tempData.match(/}/g) || []).length; // Count closing braces

                if (openBraces > closeBraces) {
                    const missingBraces = openBraces - closeBraces; // Calculate missing closing braces
                    tempData += '}'.repeat(missingBraces); // Add missing closing braces
                }

                try {
                    const jsonData = JSON.parse(tempData); // Attempt to parse again with added closing braces
                    // If successful, render and reset
                    document.getElementById("loading").innerText = `Data Length: ${humanReadableSize(tempData.length)}\n`;
                    // try {
                    //     updateNestedList(jsonData);
                    // }
                    // catch (e) {
                    //     console.error(e);
                    // }
                } catch (innerError) {
                    // If still not parsable, show loading message
                    document.getElementById("loading").innerText = `Waiting for more data... Accumulated Data Length: ${humanReadableSize(accumulatedData.length)}\n`;

                }
            }
        };

        socket.onerror = (evt) => {
            console.error("WebSocket Error:", evt);
        };
    }
    connect(); // Start WebSocket connection
}

// WebSocket communications
function executeCommand(command) {
    if (socket && socket.readyState == WebSocket.OPEN) {
        socket.send(command);
        console.log(command);
    }
}
// Utility Functions
const removeSDPrefix = (path) => path.replace(/^\/SD/, '') || '/';
const humanReadableSize = (size) => {
    const units = ["B", "KB", "MB", "GB"];
    let unitIndex = 0;
    while (size > 1024 && unitIndex < units.length - 1) {
        size /= 1024;
        unitIndex++;
    }
    return `${size.toFixed(2)} ${units[unitIndex]}`;
};
const sanitizePath = (path) => path.replace(/[^a-zA-Z0-9-_]/g, '-');
// File Actions
const handleFileAction = async (filePath, action) => {
    const folderPath = filePath.substring(0, filePath.lastIndexOf('/'));
    filePath = removeSDPrefix(filePath);
    const url = `/file?name=${filePath}&action=${action}`;
    const sanitizedFolderPath = sanitizePath(folderPath);
    const progressText = document.querySelector(`#progress-text-${sanitizedFolderPath}`);
    if (progressText) {
        progressText.textContent = `${action.charAt(0).toUpperCase() + action.slice(1)}ing...`;
    }
    try {
        if (action === "delete") {
            const response = await fetch(url);
            const result = await response.text();
            if (progressText) {
                progressText.textContent = `Deleted: ${result}`;
                location.reload();
            }
        } else if (action === "download") {
            window.open(url, "_blank");
            if (progressText) {
                progressText.textContent = "Download started...";
            }
        }
    } catch (error) {
        console.error("Error:", error);
        if (progressText) {
            progressText.textContent = `Error: ${error.message}`;
        }
    }
};
// File Upload Function
const uploadFile = (folderPath) => {
    const sanitizedPath = sanitizePath(folderPath);
    const inputFile = document.querySelector(`#upload-input-${sanitizedPath}`);
    const progressBarContainer = document.querySelector(`.progress-bar[data-path="${sanitizedPath}"]`);
    const progressBar = document.querySelector(`#progress-${sanitizedPath}`);
    const progressText = document.querySelector(`#progress-text-${sanitizedPath}`);
    const file = inputFile.files[0];
    if (!file) {
        alert("Please select a file to upload.");
        return;
    }
    progressBarContainer.style.display = "block";
    const xhr = new XMLHttpRequest();
    const formData = new FormData();
    formData.append("file", file);
    formData.append("path", folderPath);

    // xhr.upload.addEventListener("progress", (event) => {
    xhr.upload.onprogress = (event) => {
        if (event.lengthComputable) {
            const percentComplete = (event.loaded / event.total) * 100;
            const byteTransferred = event.loaded;
            const totalBytes = event.total;
            progressBar.style.width = percentComplete + "%";
            progressText.textContent = `Uploading: ${humanReadableSize(byteTransferred)} / ${humanReadableSize(totalBytes)} (${percentComplete.toFixed(2)}%)`;
        }
    };
    // xhr.addEventListener("load", () =>
    xhr.onload = () => {
        if (xhr.status === 200) {
            alert(`Upload complete: ${file.name}`);
            progressBar.style.width = "0%";
            progressText.textContent = "Upload complete!";
            progressBarContainer.style.display = "none";
            location.reload();
        } else {
            alert(`Upload failed: ${xhr.statusText}`);
            progressBarContainer.style.display = "none";
        }
    };

    // xhr.addEventListener("error", () =>
    xhr.onerror = () => {
        alert("Error: File upload failed.");
        progressText.textContent = "Error occurred during upload.";
        progressBarContainer.style.display = "none";
    };
    // xhr.addEventListener("abort", () => 
    xhr.onabort = () => {
        alert("Upload aborted.");
        progressText.textContent = "Upload aborted.";
        progressBarContainer.style.display = "none";
    };

    xhr.open("POST", removeSDPrefix(folderPath + "/" + file.name));
    xhr.send(formData);
};
// Folder Creation
const createFolder = async (folderPath) => {
    const folderNameInput = document.querySelector(`#folder-name-${folderPath.replace(/\//g, '-')}`);
    const folderName = folderNameInput.value.trim();
    if (!folderName) {
        alert("Please enter a folder name.");
        return;
    }
    folderPath = removeSDPrefix(folderPath);
    const url = `/dir?name=${folderPath}/${folderName}&action=create`;
    try {
        const response = await fetch(url);
        const result = await response.text();
        alert(`Folder Create: ${result}`);
        location.reload();
    } catch (error) {
        console.error("Error:", error);
        alert(`Error create folder: ${error.message}`);
    }
};
// Table Creation for Files
const createFileTable = (files, currentPath) => {
    const table = document.createElement("table");
    table.innerHTML = `
  <thead>
    <tr>
      <th>File Name</th>
      <th>Date Modified</th>
      <th>Size</th>
      <th>Actions</th>
    </tr>
  </thead>
  <tbody>
    ${files.map(file => `
      <tr>
        <td>${file.title}</td>
        <td>${file.DateMod}</td>
        <td>${humanReadableSize(file.size)}</td>
        <td>
          <button data-action="download" data-path="${currentPath}/${file.title}">Download</button>
          <button class="delete" data-action="delete" data-path="${currentPath}/${file.title}">Delete</button>
        </td>
      </tr>
    `).join('')}
  </tbody>
`;
    return table;
};
// Upload Section Creation
const createUploadSection = (folderPath) => {
    const sanitizedPath = sanitizePath(folderPath);
    const div = document.createElement("div");
    div.className = "upload-container";
    div.innerHTML = `
  <input type="text" id="folder-name-${sanitizedPath}" placeholder="Enter folder name">
  <button data-action="create-folder" data-path="${folderPath}">Create Folder</button>
  <input type="file" id="upload-input-${sanitizedPath}">
  <button data-action="upload-file" data-path="${folderPath}">Upload File</button>
  <div class="progress-bar" data-path="${sanitizedPath}">
    <div class="progress" id="progress-${sanitizedPath}"></div>
  </div>
  <div class="progress-info" id="progress-text-${sanitizedPath}" style="color: #333;">Ready to upload...</div>
`;
    return div;
};
// Nested List Creation
const createNestedList = (data, currentPath = "") => {
    const ul = document.createElement("ul");
    const files = [];
    const folders = [];
    for (const key in data) {
        if (data[key].type === "file") {
            files.push({ title: key, DateMod: data[key].lastModified, size: data[key].size });
        } else {
            folders.push({ title: key, children: data[key] });
        }
    }
    if (files.length > 0) {
        const table = createFileTable(files, currentPath);
        const li = document.createElement("li");
        li.appendChild(table);
        ul.appendChild(li);
    }
    folders.forEach(item => {
        const li = document.createElement("li");
        const button = document.createElement("button");
        button.textContent = "+";
        const span = document.createElement("span");
        span.textContent = item.title;
        const uploadSection = createUploadSection(`${currentPath}/${item.title}`);
        li.appendChild(button);
        li.appendChild(span);
        li.appendChild(uploadSection);
        if (item.children) {
            const childUl = createNestedList(item.children, `${currentPath}/${item.title}`);
            childUl.style.display = "none";
            li.appendChild(childUl);
            if (Object.keys(item.children).length === 0) {
                const removeBtn = document.createElement("button");
                removeBtn.textContent = "Remove Folder";
                removeBtn.classList.add("delete", "remove-folder-btn");
                removeBtn.dataset.action = "remove-folder";
                removeBtn.dataset.path = `${currentPath}/${item.title}`;
                li.appendChild(removeBtn);
            }
            button.addEventListener("click", () => {
                const isExpanded = childUl.style.display === "block";
                childUl.style.display = isExpanded ? "none" : "block";
                uploadSection.style.display = isExpanded ? "none" : "block";
                button.textContent = isExpanded ? "+" : "-";
            });
        } else {
            button.addEventListener("click", () => {
                const isExpanded = uploadSection.style.display === "block";
                uploadSection.style.display = isExpanded ? "none" : "block";
                button.textContent = isExpanded ? "+" : "-";
            });
        }
        ul.appendChild(li);
    });
    return ul;
};
// Directory Removal
const removeDirectory = async (folderPath) => {
    const confirmed = confirm(`Are you sure you want to remove the folder: ${folderPath}?`);
    if (!confirmed) return;
    folderPath = removeSDPrefix(folderPath);
    const url = `/dir?name=${folderPath}&action=delete`;
    try {
        const response = await fetch(url);
        const result = await response.text();
        alert(`Folder Removed: ${result}`);
        location.reload();
    } catch (error) {
        console.error("Error:", error);
        alert(`Error removing folder: ${error.message}`);
    }
};
// Fetch Data with WebSocket
const fetchData = () => {
    document.getElementById("loading").style.display = "block";
    executeCommand("listFiles");
};
// Render List
const renderList = (data) => {
    nestedContainer.innerHTML = "";
    nestedContainer.appendChild(createNestedList(data));
};
// Event Listeners
document.addEventListener("DOMContentLoaded", () => {
    document.getElementById("fetch-data-button").addEventListener("click", fetchData);
});
document.addEventListener("DOMContentLoaded", () => {
    nestedContainer = document.getElementById("nested-container");
    if (nestedContainer) {
        nestedContainer.addEventListener("click", (event) => {
            const target = event.target;
            if (target.tagName === "BUTTON") {
                const action = target.dataset.action;
                const path = target.dataset.path;
                if (action === "download" || action === "delete") {
                    handleFileAction(path, action);
                } else if (action === "upload-file") {
                    uploadFile(path);
                } else if (action === "create-folder") {
                    createFolder(path);
                } else if (action === "remove-folder") {
                    removeDirectory(path);
                }
            }
        });
    } else {
        console.error("nestedContainer element not found!");
    }
});