const express = require('express');
const app = express();
const path = require('path');
const os = require('os');

app.use(express.static(path.join(__dirname, './')));

const port = 6200;

app.listen(port, '0.0.0.0', () => {
    const networkInterfaces = os.networkInterfaces();
    let ipAddress;

    // 遍历网络接口，找到无线网和有线网口的IPv4地址
    Object.keys(networkInterfaces).forEach((interfaceName) => {
        networkInterfaces[interfaceName].forEach((networkInterface) => {
            if (
                !networkInterface.internal &&
                (networkInterface.family === 'IPv4') &&
                (interfaceName.includes('WLAN') || interfaceName.includes('ETH'))
                //排除掉虚拟机的Ipv4地址
            ) {
                ipAddress = networkInterface.address;
                console.log(`Device Name: ${interfaceName}`);
                console.log(`IP Address: ${ipAddress}`);
            }
        });
    });

    //方便直接打开网址
    console.log(`Available on:`);
    console.log(`http://localhost:${port}`);
    console.log(`http://${ipAddress}:${port}`);
});
