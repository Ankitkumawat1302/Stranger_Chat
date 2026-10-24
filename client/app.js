document.addEventListener('DOMContentLoaded', () => {
    const messagesContainer = document.getElementById('messages-container');
    const messageInput = document.getElementById('message-input');
    const sendBtn = document.getElementById('send-btn');
    const skipBtn = document.getElementById('skip-btn');
    const statusText = document.getElementById('status-text');
    const statusDot = document.getElementById('status-dot');
    const typingIndicator = document.getElementById('typing-indicator');

    let socket = null;
    let isTypingLocal = false;
    let typingTimeout = null;

    // Step 7: Exponential Backoff Reconnection Variables
    let reconnectAttempts = 0;
    let reconnectTimer = null;
    const MAX_RECONNECT_DELAY = 10000; // 10 seconds ceiling

    function appendMessage(text, isMe) {
        if (!messagesContainer) return;
        const row = document.createElement('div');
        row.className = isMe ? 'message-row sent' : 'message-row received';

        const bubble = document.createElement('div');
        bubble.className = 'bubble';
        bubble.textContent = text;

        row.appendChild(bubble);
        messagesContainer.appendChild(row);
        messagesContainer.scrollTop = messagesContainer.scrollHeight;
    }

    function appendSystemMessage(text) {
        if (!messagesContainer) return;
        const div = document.createElement('div');
        div.className = 'system-message';
        div.textContent = text;
        messagesContainer.appendChild(div);
        messagesContainer.scrollTop = messagesContainer.scrollHeight;
    }

    function setTypingStatus(show) {
        if (!typingIndicator) return;
        if (show) {
            typingIndicator.classList.remove('hidden');
        } else {
            typingIndicator.classList.add('hidden');
        }
    }

    function sendTypingEvent(isTyping) {
        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify({ type: 'typing', isTyping: isTyping ? 'true' : 'false' }));
        }
    }

    function connect() {
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }

        statusText.textContent = 'Connecting to server...';
        statusDot.className = 'status-indicator offline';

        socket = new WebSocket('ws://127.0.0.1:8080/chat');

        socket.onopen = () => {
            console.log('[WS] Connected to C++ backend!');
            reconnectAttempts = 0; // Reset counter on successful link
            statusText.textContent = 'Connected. Waiting in queue...';
            statusDot.className = 'status-indicator online';
        };

        socket.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);

                if (data.type === 'waiting') {
                    statusText.textContent = 'Waiting for a stranger...';
                    appendSystemMessage('Waiting for a stranger to join...');
                    messageInput.disabled = true;
                    sendBtn.disabled = true;
                    setTypingStatus(false);
                } else if (data.type === 'paired') {
                    statusText.textContent = 'Connected to Stranger';
                    appendSystemMessage('You are now paired with a stranger. Say hi!');
                    messageInput.disabled = false;
                    sendBtn.disabled = false;
                    messageInput.focus();
                    setTypingStatus(false);
                } else if (data.type === 'chat') {
                    setTypingStatus(false);
                    appendMessage(data.message, false);
                } else if (data.type === 'typing') {
                    setTypingStatus(data.isTyping === 'true');
                } else if (data.type === 'stranger_disconnected') {
                    statusText.textContent = 'Stranger disconnected';
                    appendSystemMessage('Stranger has disconnected.');
                    messageInput.disabled = true;
                    sendBtn.disabled = true;
                    setTypingStatus(false);
                }
            } catch (e) {
                console.warn('Non-JSON message received:', event.data);
            }
        };

        socket.onclose = () => {
            console.log('[WS] Connection closed');
            statusDot.className = 'status-indicator offline';
            messageInput.disabled = true;
            sendBtn.disabled = true;
            setTypingStatus(false);

            // Calculate exponential backoff delay (1.5s, 2.3s, 3.4s, up to 10s)
            reconnectAttempts++;
            const delay = Math.min(1000 * Math.pow(1.5, reconnectAttempts), MAX_RECONNECT_DELAY);
            const seconds = (delay / 1000).toFixed(1);

            statusText.textContent = `Disconnected (Retry in ${seconds}s)`;
            appendSystemMessage(`Connection lost. Reconnecting in ${seconds}s...`);

            reconnectTimer = setTimeout(connect, delay);
        };

        socket.onerror = (err) => {
            console.error('[WS] Socket error observed:', err);
            socket.close();
        };
    }

    function sendMessage() {
        const text = messageInput.value.trim();
        if (!text || !socket || socket.readyState !== WebSocket.OPEN) return;

        if (isTypingLocal) {
            isTypingLocal = false;
            clearTimeout(typingTimeout);
            sendTypingEvent(false);
        }

        socket.send(JSON.stringify({ type: 'chat', message: text }));
        appendMessage(text, true);
        messageInput.value = '';
        messageInput.focus();
    }

    messageInput.addEventListener('input', () => {
        if (!isTypingLocal) {
            isTypingLocal = true;
            sendTypingEvent(true);
        }

        clearTimeout(typingTimeout);
        typingTimeout = setTimeout(() => {
            isTypingLocal = false;
            sendTypingEvent(false);
        }, 1500);
    });

    sendBtn.addEventListener('click', (e) => {
        e.preventDefault();
        sendMessage();
    });

    messageInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            e.preventDefault();
            sendMessage();
        }
    });

    skipBtn.addEventListener('click', (e) => {
        e.preventDefault();
        if (socket && socket.readyState === WebSocket.OPEN) {
            messagesContainer.innerHTML = '';
            setTypingStatus(false);
            appendSystemMessage('Looking for a new stranger...');
            statusText.textContent = 'Searching...';
            socket.send(JSON.stringify({ type: 'next' }));
        }
    });

    connect();
});