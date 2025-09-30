// @ts-check
"use strict"

// This is a simple and minimalistic time server using SNTP protocol without using any external libraries
// It listens on port 123 and responds to SNTP client requests
// It does not handle time synchronization, it just responds with current time

const dgram = require('dgram')

// SNTP server
class SNTP {
    #port = 123
    constructor(port = 123) {
        this.#port = port || 123
        const server = dgram.createSocket('udp4')
        server.on('message', this.#onMessage.bind(this))
        this.server = server
    }

    /** @param { number } port */
    start(port) { this.server.bind(port || this.#port) }
    stop() { this.server.close() }

    /** @param { Buffer } msg @param { dgram.RemoteInfo } rinfo */
    #onMessage(msg, rinfo) {
        const source = `${rinfo.address}:${rinfo.port}`
        // Parse SNTP packet
        const packet = new Uint8Array(msg)
        const LI = packet[0] >> 6
        const VN = (packet[0] >> 3) & 0b111
        const Mode = packet[0] & 0b111
        // Do not respond if not SNTP client request
        if (/* LI !== 0 || */ VN !== 4 || Mode !== 3) return console.error(`SNTP client request from ${source} FAILED`)
        console.log(`SNTP client request from ${source} OK`)
        const time = Math.floor((+new Date()) / 1000) + 2208988800 // NTP timestamp
        // Send response
        const response = new Uint8Array(48)
        response[0] = 0b11100011 // LI, VN, Mode
        response[40] = (time >> 24) & 0xFF
        response[41] = (time >> 16) & 0xFF
        response[42] = (time >> 8) & 0xFF
        response[43] = time & 0xFF
        this.server.send(response, rinfo.port, rinfo.address, (err) => {
            if (err) console.error(err)
        })
    }
    /** @param { string } event @param { (...args: any[]) => void } cb */
    on(event, cb) { this.server.on(event, cb) }

    /** @param { string } event @param { (...args: any[]) => void } cb */
    off(event, cb) { this.server.off(event, cb) }
    close() { this.server.close() }
}

const sntp = new SNTP()

sntp.on('listening', () => {
    console.log(`SNTP server listening on localhost:${sntp.server.address().port}`);
})

sntp.on('error', (err) => {
    console.error(err)
})

sntp.start(123)
