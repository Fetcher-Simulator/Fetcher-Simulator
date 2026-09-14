-- Low-priority, bounded MIDI delivery. One chunk per tick for the whole server,
-- rotating receivers so simultaneous joins do not multiply the burst size.
local mp = require('mp')
local M = {}
local CHUNK_BYTES = 16384
local SEND_INTERVAL = 0.025
local CATALOG_TTL = 10
local CACHE_LIMIT = 8 * 1024 * 1024
local catalog, catalogAt, byName = nil, -math.huge, {}
local cache, cacheBytes = {}, 0
local queue, pending, nextSend = {}, {}, 0

function M.reset()
    catalog, catalogAt, byName = nil, -math.huge, {}
    cache, cacheBytes = {}, 0
    queue, pending, nextSend = {}, {}, 0
end

function M.catalog()
    local now = mp.getUptime()
    if not catalog or now - catalogAt >= CATALOG_TTL then
        catalog = type(mp.listBardcraftHostedMidiFiles) == 'function' and mp.listBardcraftHostedMidiFiles() or {}
        catalogAt, byName = now, {}
        cache, cacheBytes = {}, 0
        for _, entry in ipairs(catalog) do byName[entry.name] = entry end
    end
    return catalog
end

function M.cancel(guid)
    pending[guid] = nil
    for i = #queue, 1, -1 do
        if queue[i] == guid then table.remove(queue, i) end
    end
end

function M.request(guid, data)
    M.catalog()
    M.cancel(guid)
    local names, seen = {}, {}
    for _, name in ipairs(type(data.names) == 'table' and data.names or {}) do
        if type(name) == 'string' and byName[name] and not seen[name] then
            names[#names + 1], seen[name] = name, true
            if #names == 8 then break end
        end
    end
    local token = type(data.token) == 'string' and data.token:sub(1, 128) or ''
    pending[guid] = {names = names, index = 1, offset = 0, token = token, skipped = 0}
    queue[#queue + 1] = guid
end

function M.tick(enabled)
    if #queue == 0 then return end
    local now = mp.getUptime()
    if now < nextSend then return end
    nextSend = now + SEND_INTERVAL
    local guid = table.remove(queue, 1)
    local transfer = pending[guid]
    if not transfer then return end
    if not enabled then
        mp.send(guid, 'BC_BardcraftServerSongFilesEnd', {token = transfer.token, disabled = true})
        pending[guid] = nil
        return
    end
    local name = transfer.names[transfer.index]
    if not name then
        mp.send(guid, 'BC_BardcraftServerSongFilesEnd', {token = transfer.token, skipped = transfer.skipped})
        pending[guid] = nil
        return
    end
    if not transfer.bytes then
        local bytes = cache[name]
        if not bytes and type(mp.readBardcraftHostedMidiFile) == 'function' then
            bytes = mp.readBardcraftHostedMidiFile(name)
            if type(bytes) == 'string' and #bytes <= 512 * 1024 then
                if cacheBytes + #bytes > CACHE_LIMIT then cache, cacheBytes = {}, 0 end
                cache[name], cacheBytes = bytes, cacheBytes + #bytes
            else bytes = nil end
        end
        transfer.bytes = bytes
    end
    if transfer.bytes then
        local bytes = transfer.bytes
        local chunk = bytes:sub(transfer.offset + 1, transfer.offset + CHUNK_BYTES)
        mp.send(guid, 'BC_BardcraftServerSongFileChunk', {
            token = transfer.token, name = name, size = #bytes,
            offset = transfer.offset, bytes = chunk,
        })
        transfer.offset = transfer.offset + #chunk
        if transfer.offset >= #bytes then
            transfer.index, transfer.offset, transfer.bytes = transfer.index + 1, 0, nil
        end
    else
        transfer.skipped = transfer.skipped + 1
        transfer.index = transfer.index + 1
    end
    queue[#queue + 1] = guid
end

return M
