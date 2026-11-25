local seen = nil
hikari.events.on('document.changed', function(data) seen = data.step end)
hikari.commands.register('greet', function(args) return {hello = args.name or 'you'} end)
hikari.commands.register('shout', function()
    local done, err = hikari.document.edit('Shout', function(doc, change)
        for _, line in ipairs(doc.lines) do change:set_line(line.id, {text = line.text:upper()}) end
    end)
    if not done then error(err.code) end
end)
hikari.commands.register('ask', function() return hikari.input_box.ask({title = 'Name'}) end)
hikari.commands.register('pick', function()
    return hikari.quick_pick.ask({title = 'Colour', items = {{id = 'red', label = 'Red'}, {id = 'blue', label = 'Blue'}}})
end)
hikari.commands.register('seen', function() return seen end)
hikari.commands.register('spin', function() while true do end end)
hikari.formats.exporter('cues', function(doc)
    local out = {}
    for _, line in ipairs(doc.lines) do out[#out + 1] = line.start .. '|' .. line['end'] .. '|' .. line.text end
    return table.concat(out, '\n') .. '\n'
end)
hikari.providers.completion(function(r)
    return {{label = '\\fad', insert = '\\fad(200,200)', from = r.offset - 1, to = r.offset}}
end)
