const messagesContainer = document.getElementById('messages-container');
const messageInput = document.getElementById('message-input');
const sendBtn = document.getElementById('send-btn');
const actionBtn = document.getElementById('action-btn');
const statusDot = document.getElementById('status-dot');
const statusText = document.getElementById('status-text');

// Function to append a message bubble
function appendMessage(text, sender = 'user') {
  if (!text.trim()) return;

  const msgDiv = document.createElement('div');
  msgDiv.classList.add('message', sender);
  msgDiv.textContent = text;
  messagesContainer.appendChild(msgDiv);

  // Auto-scroll to the latest message
  messagesContainer.scrollTop = messagesContainer.scrollHeight;
}

// Send message on click
sendBtn.addEventListener('click', () => {
  const text = messageInput.value;
  if (text.trim() !== '') {
    appendMessage(text, 'user');
    messageInput.value = '';
    messageInput.focus();
  }
});

// Send message on Enter key
messageInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') {
    sendBtn.click();
  }
});

// Mock toggle connection state (for testing UI prior to C++ backend)
actionBtn.addEventListener('click', () => {
  const isDisconnected = statusText.textContent === 'Disconnected';
  if (isDisconnected) {
    statusDot.classList.add('connected');
    statusText.textContent = 'Connected to Stranger';
    actionBtn.textContent = 'Skip / Next';
    actionBtn.classList.remove('secondary');
    actionBtn.classList.add('primary');
    appendMessage('You are now paired with a stranger. Say hi!', 'system-msg');
  } else {
    statusDot.classList.remove('connected');
    statusText.textContent = 'Disconnected';
    actionBtn.textContent = 'Find Stranger';
    actionBtn.classList.remove('primary');
    actionBtn.classList.add('secondary');
    appendMessage('Stranger has disconnected.', 'system-msg');
  }
});