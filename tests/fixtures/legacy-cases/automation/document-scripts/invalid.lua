-- Validation edits the subtitles (the first Line's text and a Script Info
-- key), then answers false: legacy's AutoToFile edits the file in place.
script_name = "S4 invalid"
aegisub.register_macro("Guarded", "", function(subs, sel)
    record("run", subs)
    prefix(subs, sel)
end, function(subs, sel)
    record("validate", subs)
    for i = 1, #subs do
        local l = subs[i]
        if l.class == "dialogue" then
            l.text = "edited while validating"
            subs[i] = l
            break
        end
    end
    subs.insert(3, { class = "info", section = "[Script Info]", key = "Validated", value = "yes" })
    return false
end)
