env = require('test_run')
fiber = require('fiber')
net_box = require('net.box')
test_run = env.new()

test_run:cmd("setopt delimiter ';'")
function ddos(server_listen)
    local fibers = {}
    for i = 1, 100 do
        fibers[i] = fiber.new(function()
            local tnt = net_box.new(server_listen)
            for _ = 1, 100 do
                pcall(tnt.call, tnt, 'ping')
            end
        end)
        fibers[i]:set_joinable(true)
    end
    for _, f in ipairs(fibers) do
        f:join()
    end
end;
test_run:cmd('create server server with script=\z
              "box/gh-5645-several-iproto-server.lua"');
test_run:cmd("setopt delimiter ''");
-- checks that several iproto threads are better able
-- to handle high load than one
test_run:cmd('start server server with args="2"')
server_listen = test_run:cmd("eval server 'return box.cfg.listen'")[1]
ddos(server_listen)
assert(test_run:grep_log('server', "stopping input on connection") == nil)
test_run:cmd('stop server server')
test_run:cmd('start server server with args="1"')
server_listen = test_run:cmd("eval server 'return box.cfg.listen'")[1]
ddos(server_listen)
assert(test_run:grep_log('server', "stopping input on connection") ~= nil)
test_run:cmd('stop server server')
test_run:cmd('cleanup server server')
test_run:cmd("delete server server")

