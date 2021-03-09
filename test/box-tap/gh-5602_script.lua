local tap = require('tap')
local test = tap.test('gh-5602')

local status, err = pcall(box.cfg, {background = false, vinyl_timeout = 70.1})

-- Check that environment cfg values are set correctly.
if arg[1] == '1' then
    test:plan(5)
    test:ok(status ~= false, 'box.cfg is successful')
    test:is(box.cfg['listen'], '3301', 'listen')
    test:is(box.cfg['readahead'], 10000, 'readahead')
    test:is(box.cfg['strip_core'], false, 'strip_core')
    test:is(box.cfg['log_format'], 'json', 'log_format is not set')
end
if arg[1] == '2' then
    test:plan(7)
    test:ok(status ~= false, 'box.cfg is successful')
    test:is(box.cfg['listen'], '3301', 'listen')
    local replication = box.cfg['replication']
    test:is(type(replication), 'table', 'replication is table')
    test:ok(replication[1] == '0.0.0.0:12345' or
            replication[1] == '1.1.1.1:12345', 'replication URI 1')
    test:ok(replication[2] == '0.0.0.0:12345' or
            replication[2] == '1.1.1.1:12345', 'replication URI 2')
    test:is(box.cfg['replication_connect_timeout'], 0.01,
            'replication_connect_timeout')
    test:is(box.cfg['replication_synchro_quorum'], '4 + 1',
            'replication_synchro_quorum')
end

-- Check that box.cfg{} values are more prioritized than
-- environment cfg values.
if arg[1] == '3' then
    test:plan(3)
    test:ok(status ~= false, 'box.cfg is successful')
    test:is(box.cfg['background'], false,
            'box.cfg{} background value is prioritized')
    test:is(box.cfg['vinyl_timeout'], 70.1,
            'box.cfg{} vinyl_timeout value is prioritized')
end

-- Check bad environment cfg values.
if arg[1] == '4' then
    test:plan(2)
    test:ok(status == false, 'box.cfg is not successful')
    local index = string.find(err, 'Environment variable TT_SQL_CACHE_SIZE '..
        'has incorrect value for option \'sql_cache_size\': should be '..
        'converted to number')
    test:ok(index ~= nil, 'bad sql_cache_size value')
end
if arg[1] == '5' then
    test:plan(2)
    test:ok(status == false, 'box.cfg is not successful')
    print(err)
    local index = string.find(err, 'Environment variable TT_STRIP_CORE has '..
        'incorrect value for option \'strip_core\': should be \'true\' '..
        'or \'false\'')
    test:ok(index ~= nil, 'bad strip_core value')
end
if arg[1] == '6' then
    test:plan(2)
    test:ok(status == false, 'box.cfg is not successful')
    print(err)
    local index = string.find(err, 'Environment variable TT_REPLICATION has '..
        'incorrect value for option \'replication\': elements of array should'..
        ' be quoted strings')
    test:ok(index ~= nil, 'bad replication value')
end

os.exit(test:check() and 0 or 1)