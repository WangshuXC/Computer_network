const express = require('express');
const app = express();
const path = require('path');
const os = require('os');

app.use(express.static(path.join(__dirname, './')));

const port = 6200;

app.listen(port, '0.0.0.0', () => {
    const networkInterfaces = os.networkInterfaces();
    let ipAddress;

    // 遍历网络接口，找到非 localhost 下的 IPv4 地址
    Object.keys(networkInterfaces).forEach((interfaceName) => {
        networkInterfaces[interfaceName].forEach((networkInterface) => {
            if (!networkInterface.internal && networkInterface.family === 'IPv4') {
                ipAddress = networkInterface.address;
            }
        });
    });
    console.log(`Availble on:`);
    console.log(`http://localhost:${port}`);
    console.log(`http://${ipAddress}:${port}`);
});
