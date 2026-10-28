script_name = "S4 raising"
aegisub.register_macro("Raising", "", function(subs, sel)
    record("run", subs)
end, function(subs, sel)
    record("validate", subs)
    error("no way")
end)
