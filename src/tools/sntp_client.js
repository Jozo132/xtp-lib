// @ts-check
"use strict"

const dgram = require('dgram')
const client = dgram.createSocket('udp4')

// SNTP client
// const host = '0.europe.pool.ntp.org'
const host = 'localhost'
const port = 123

/** @param { Buffer } msg */
const sntp_decode = msg => {
    const packet = new Uint8Array(msg)
    if (!(packet[0] & 0b11000000 || packet[0] & 0b00100100)) {
        console.error('Invalid SNTP packet received', packet)
        throw new Error('Invalid SNTP packet received')
    }

    // Parse seconds
    let seconds_hex = '0x'
    for (let i = 40; i < 44; i++) {
        const byte = +packet[i]
        const hex = byte.toString(16).toUpperCase().padStart(2, '0')
        seconds_hex += hex
    }
    const seconds = +seconds_hex
    let NTP_OFFSET = 2208988800
    const timestamp_s = seconds - NTP_OFFSET
    const timestamp_ms = Math.floor(timestamp_s * 1000)
    return timestamp_ms
}

client.on('message', (msg, rinfo) => {
    console.log(`SNTP server response from ${rinfo.address}:${rinfo.port}`)
    const ts = sntp_decode(msg)
    const time = new Date(ts)
    console.log(`SNTP timestamp: ${ts} -> ${time}`)
    client.close()
})

const request_bytes = Buffer.alloc(48)
request_bytes[0] = 0b00100011 // LI, VN, Mode


client.send(request_bytes, 0, 48, port, host, (err) => err ? console.error(err) : null)

client.on('listening', () => {
    console.log(`SNTP client listening on localhost:${client.address().port}`)
})

client.on('error', (err) => {
    console.error(err)
})
