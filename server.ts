import * as http from 'http';
import * as WebSocket from 'ws';
import * as os from 'os';

// Create an HTTP server
const server = http.createServer((req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('WebSocket server running\n');
});

// Create a WebSocket server instance attached to the HTTP server
const wss = new WebSocket.Server({ server });

// Store active connections
const clients = new Set<WebSocket>();

// Handle new WebSocket connections
wss.on('connection', (ws: WebSocket, req: http.IncomingMessage) => {
  const ip = req.socket.remoteAddress;
  const port = req.socket.remotePort;
  const clientId = `${ip}:${port}`;
  
  console.log(`New connection from ${clientId}`);
  clients.add(ws);
  
  // Send welcome message
  ws.send(`Hello from TypeScript WebSocket server! You are ${clientId}`);
  
  // Handle messages from the client
  ws.on('message', (message: WebSocket.Data) => {
    console.log(`Received from ${clientId}: ${message}`);
    
    // Echo the message back
    ws.send(`Echo: ${message}`);
    
    // Broadcast message to all other clients
    clients.forEach(client => {
      if (client !== ws && client.readyState === WebSocket.OPEN) {
        client.send(`Message from ${clientId}: ${message}`);
      }
    });
  });
  
  // Handle client disconnection
  ws.on('close', () => {
    console.log(`Connection closed: ${clientId}`);
    clients.delete(ws);
  });
  
  // Handle errors
  ws.on('error', (error) => {
    console.error(`Error with connection ${clientId}:`, error);
    clients.delete(ws);
  });
});

// Get network interfaces
function getLocalIpAddresses(): string[] {
  const interfaces = os.networkInterfaces();
  const addresses: string[] = [];
  
  for (const iface of Object.values(interfaces)) {
    if (iface) {
      for (const alias of iface) {
        if (alias.family === 'IPv4' && !alias.internal) {
          addresses.push(alias.address);
        }
      }
    }
  }
  
  return addresses;
}

// Start the server
const PORT = 8080;
server.listen(PORT, '0.0.0.0', () => {
  console.log(`WebSocket server listening on port ${PORT}`);
  
  const localIps = getLocalIpAddresses();
  console.log('Available connection addresses:');
  localIps.forEach(ip => {
    console.log(`ws://${ip}:${PORT}/`);
  });
  
  console.log('For local connections, use:');
  console.log(`ws://localhost:${PORT}/`);
});

// Handle server shutdown
process.on('SIGINT', () => {
  console.log('Shutting down server...');
  server.close(() => {
    console.log('Server shut down');
    process.exit(0);
  });
});