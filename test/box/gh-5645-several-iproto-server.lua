#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv('LISTEN'),
    iproto_threads = tonumber(arg[1])
})

local function ping() return "pong" end
ping()
