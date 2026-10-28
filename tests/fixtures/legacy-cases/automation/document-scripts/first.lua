script_name = "S4 first"
aegisub.register_macro("First", "", function(subs, sel)
    record("run", subs)
    prefix(subs, sel)
end, function(subs, sel)
    record("validate", subs)
    return true
end)
