document.addEventListener('DOMContentLoaded', () => {
    const messagesContainer = document.getElementById('messages-container');
    const messageInput = document.getElementById('message-input');
    const sendBtn = document.getElementById('send-btn');
    const skipBtn = document.getElementById('skip-btn');
    const statusText = document.getElementById('status-text');
    const statusDot = document.getElementById('status-dot');

    let socket = null;

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

    function connect() {
        socket = new WebSocket('ws://127.0.0.1:8080/chat');

        socket.onopen = () => {
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
                } else if (data.type === 'paired') {
                    statusText.textContent = 'Connected to Stranger';
                    appendSystemMessage('You are now paired with a stranger. Say hi!');
                    messageInput.disabled = false;
                    sendBtn.disabled = false;
                    messageInput.focus();
                } else if (data.type === 'chat') {
                    appendMessage(data.message, false);
                } else if (data.type === 'stranger_disconnected') {
                    statusText.textContent = 'Stranger disconnected';
                    appendSystemMessage('Stranger has disconnected.');
                    messageInput.disabled = true;
                    sendBtn.disabled = true;
                }
            } catch (e) {
                console.warn('Non-JSON message received:', event.data);
            }
        };

        socket.onclose = () => {
            statusText.textContent = 'Disconnected';
            statusDot.className = 'status-indicator offline';
            appendSystemMessage('Connection lost. Reconnecting in 2s...');
            messageInput.disabled = true;
            sendBtn.disabled = true;
            setTimeout(connect, 2000);
        };
    }

    function sendMessage() {
        const text = messageInput.value.trim();
        if (!text || !socket || socket.readyState !== WebSocket.OPEN) return;

        socket.send(JSON.stringify({ type: 'chat', message: text }));
        appendMessage(text, true); // true -> aligns to right
        messageInput.value = '';
        messageInput.focus();
    }

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
            appendSystemMessage('Looking for a new stranger...');
            statusText.textContent = 'Searching...';
            socket.send(JSON.stringify({ type: 'next' }));
        }
    });

    connect();
});