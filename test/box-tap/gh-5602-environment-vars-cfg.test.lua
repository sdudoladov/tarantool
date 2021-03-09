#!/usr/bin/env tarantool

local os = require('os')
local fio = require('fio')
local tap = require('tap')
local test = tap.test('gh-5602')

-- gh-5602: Check that environment cfg variables working.
local TARANTOOL_PATH = arg[-1]
local script_name = 'gh-5602_script.lua'
local path_to_script = fio.pathjoin(
        os.getenv('PWD'),
        'box-tap',
        script_name)
local function shell_command(set, i)
    string.gsub(set, 'TT_', '')
    return ('%s %s %s %d'):format(
        set,
        TARANTOOL_PATH,
        path_to_script,
        i)
end

local set_1 = 'TT_LISTEN=3301 ' ..
              'TT_READAHEAD=10000 ' ..
              'TT_STRIP_CORE=false ' ..
              'TT_LOG_FORMAT=json'
local set_2 = 'TT_LISTEN=3301 ' ..
              'TT_REPLICATION=\"{\'0.0.0.0:12345\', ' ..
                              '\'1.1.1.1:12345\'}\" ' ..
              'TT_REPLICATION_CONNECT_TIMEOUT=0.01 ' ..
              'TT_REPLICATION_SYNCHRO_QUORUM=\'4 + 1\''
local set_3 = 'TT_BACKGROUND=true ' ..
              'TT_VINYL_TIMEOUT=60.1'
local set_4 = 'TT_SQL_CACHE_SIZE=a'
local set_5 = 'TT_STRIP_CORE=a'
local set_6 = 'TT_REPLICATION=\"{0.0.0.0:12345\', ' ..
                              '\'1.1.1.1:12345\'}\"'
local sets = {}
sets[1] = set_1
sets[2] = set_2
sets[3] = set_3
sets[4] = set_4
sets[5] = set_5
sets[6] = set_6

for i, set in ipairs(sets) do
    local tmpdir = fio.tempdir()
    local new_path = fio.pathjoin(tmpdir, script_name)
    fio.copyfile(path_to_script, new_path)
    test:is(os.execute(shell_command(set, i)), 0, 'set '..i)
end
os.exit(0)
