script_name = "karaskel untagged"

include("karaskel.lua")

aegisub.register_macro("Kara times", "", function(subs)
	local meta, styles = karaskel.collect_head(subs, true)
	local times = {}
	for i = 1, #subs do
		local line = subs[i]
		if line.class == "dialogue" then
			karaskel.preproc_line(subs, meta, styles, line)
			for k = 0, line.kara.n do
				table.insert(times, string.format('"%d:%d-%d"', k, line.kara[k].start_time, line.kara[k].end_time))
			end
		end
	end
	aegisub.hikari.report('{"times":[' .. table.concat(times, ",") .. "]}")
end)
