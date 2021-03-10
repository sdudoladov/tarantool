#! ../src/tarantool

os.execute("rm -rf *.xlog *.snap")

local count = 1
if arg[1] ~= nil then
    count = tonumber(arg[1]) or 1
    box.cfg({
        listen = "127.0.0.1:3301",
        iproto_threads = count
    })
else
    box.cfg({
        listen = "127.0.0.1:3301",
    })
end

local fiber = require('fiber')
local net_box = require('net.box')

local function ping()
    return "pong"
end
ping()
local function latency()
    local delayed = 0
    local count = 1000
    for _ = 1, count do
        local time_before = fiber.time64()
        local tnt = net_box.new("127.0.0.1", 3301)
        pcall(tnt.call, tnt, 'ping')
        delayed = delayed + (fiber.time64() - time_before)
     end
     return delayed / count
end

print(string.format("Simple iproto call latency for %d iproto threads = %d", count, tonumber(latency())))

os.exit()
