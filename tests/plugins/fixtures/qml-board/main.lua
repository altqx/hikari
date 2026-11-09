hikari.panels.on('board', function(e)
    if e.kind == 'message' then
        hikari.panels.post('board', {echo = e.data.text .. '!'})
    end
end)

hikari.panels.on('notes', function(e)
    if e.kind == 'show' then
        hikari.panels.render('notes', {kind = 'column', children = {{kind = 'label', text = 'Notes here'}}})
    end
end)

hikari.commands.register('ping', function()
    hikari.notify(tostring(hikari.panels.post('board', {echo = 'ping'})))
end)
