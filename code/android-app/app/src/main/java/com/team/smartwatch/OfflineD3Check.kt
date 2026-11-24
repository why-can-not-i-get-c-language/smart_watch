package com.team.smartwatch

// Fixed wire examples from the APP-03 D3 task specification. No GATT device is used here.
internal suspend fun checkOfflineD3() {
    val pairs = listOf(
        ("A5 5A 01 00 01 01 00 44 33 22 11 00 00 DD C6 CB A8" to
            "A5 5A 01 01 01 01 00 44 33 22 11 1E 00 00 00 01 02 03 04 05 06 07 08 09 0A 0B 01 00 00 00 1F 00 00 00 08 44 52 41 46 54 2D 30 31 F0 A5 DC 51"),
        ("A5 5A 01 00 02 02 00 44 33 22 11 00 00 FB FD C9 1F" to
            "A5 5A 01 01 02 02 00 44 33 22 11 2B 00 00 10 27 00 00 02 00 DF 00 6B 02 00 7B 00 00 00 14 00 00 00 02 17 0F 14 00 00 00 00 00 00 00 00 FF FF FF FF 00 00 00 FF FF FF FF D9 AA 62 FC"),
        ("A5 5A 01 00 03 03 00 44 33 22 11 04 00 00 DF 00 6B EA D2 18 DB" to
            "A5 5A 01 01 03 03 00 44 33 22 11 05 00 00 01 DF 00 6B 7E 9A BD 91"),
        ("A5 5A 01 00 02 04 00 44 33 22 11 00 00 7C F4 A6 D9" to
            "A5 5A 01 01 02 04 00 44 33 22 11 2B 00 00 F8 2A 00 00 02 01 DF 00 6B 02 00 7B 00 00 00 14 00 00 00 02 17 0F 14 00 00 00 00 00 00 00 00 FF FF FF FF 00 00 00 FF FF FF FF 67 D4 C9 84"),
    )
    var step = 0
    var elapsed = 0L
    val now = 1_795_219_200_000L
    lateinit var client: D3Client
    client = D3Client(write = { bytes ->
        val (request, response) = pairs[step++]
        check(bytes.serialHex() == request) { "D3 请求样例不匹配" }
        val wire = checkNotNull(serialPayload(response, hex = true))
        for (offset in wire.indices step 16) {
            elapsed += 5
            client.receive(wire.copyOfRange(offset, minOf(offset + 16, wire.size)))
        }
        true
    }, clock = { D3Timestamp(elapsed, now + elapsed) }, nextSessionId = { 0x11223344L })
    client.newSession()
    val identity = d3Identity(client.request(D3Command.INFO).payload)
    check(identity.uid == "000102030405060708090A0B" && identity.firmware == "DRAFT-01")
    val first = d3Status(client.request(D3Command.STATUS).payload)
    check(first.stepsScope == 0 && first.steps.value == 123L && first.voltage.value == 3863L)
    check(first.oxygen.state == D3DataState.UNSUPPORTED && first.oxygen.value == null)
    val set = client.request(D3Command.SET_TIME) { d3TimePayload(it.unixMs / 1000L) }
    check(d3StoredTime(set.payload, now / 1000L) == now / 1000L + 1)
    val readback = client.request(D3Command.STATUS)
    check(d3TimeVerification(set, readback, d3Status(readback.payload)).outcome == D3TimeCheck.VERIFIED)
    check(step == pairs.size)
}
