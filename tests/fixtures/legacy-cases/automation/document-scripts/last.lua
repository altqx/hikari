script_name = "S4 last"
aegisub.register_macro("Last", "", function(subs, sel)
    record("run", subs)
    prefix(subs, sel)
end, function(subs, sel)
    record("validate", subs)
    return true
end)
aegisub.register_macro("Second macro", "", function(subs, sel)
    record("run-second", subs)
end)
