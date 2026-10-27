--[[
 Copyright (c) 2007, Niels Martin Hansen, Rodrigo Braz Monteiro
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   * Neither the name of the Aegisub Group nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
]]

-- Aegisub Automation 4 Lua karaoke templater tool
-- Parse and apply a karaoke effect written in ASS karaoke template language
-- See help file and wiki for more information on this
--
-- HikariSub's copy of Karaoke Templater 2.1.7 as Aegisub 3.2.2 shipped it: templates
-- give the same output as there, byte for byte (tests/backends/karaoke-corpus).
-- HikariSub's changes (KE1, docs/qt/karaoke-templater.md):
-- * the macro is offered when any Line is a template, not only one of the first 50
-- * every problem with a template or code Line names that Line (its number in the
--   Grid), and the macro selects the Lines that had problems
-- * each generated Line is marked with the karaoke Line it came from (aegisub.hikari),
--   and "Apply karaoke template to selected lines" replaces only the selected karaoke
--   Lines' generated Lines
-- * a run reports how long each template took and how many Lines it generated
-- * the description no longer offers an export filter, which HikariSub has none of

local tr = aegisub.gettext

script_name = tr"Karaoke Templater"
script_description = tr"Macro to apply karaoke effects using the template language"
script_author = "Niels Martin Hansen"
script_version = "2.1.7"


include("karaskel.lua")


-- The template and code Lines problems were reported for, by subtitle index
local problem_lines = {}
-- A selected-Lines run moves Lines after its problems are found, so it keeps
-- them here and reports them at the end; while it works out the other Lines'
-- output nothing is reported at all.
local deferred_problems, quiet = nil, false
-- What the run did, for its report (report_run)
local stats

local function report_problem(level, where, message, number)
	if where then
		local kind = where.kind == "code" and "Code" or "Template"
		message = message .. string.format("%s Line: %d\n", kind, number)
	end
	aegisub.debug.out(level, "%s\n", message)
end

-- Reports a problem with a template or code Line: the 2.1.7 message, then the
-- Line's number in the Grid. `where` is nil for a template the run made up;
-- `at` is where in that Line's text the problem is ({start, length} in
-- bytes from 1), nil when it has no place there.
local function problem_at(level, where, at, fmt, ...)
	if quiet then
		return
	end
	local message = string.format(fmt, ...)
	if where then
		problem_lines[where.index] = true
	end
	if stats then
		stats.problems = stats.problems + 1
		-- the report's list (report_run): each problem once, however many
		-- syllables or Lines ran into it
		local key = where and table.concat({ where.index, at and at.start or 0, at and at.length or 0,
			at and "" or message }, ":")
		if key and not stats.issue_keys[key] and #stats.issues < 100 then
			stats.issue_keys[key] = true
			table.insert(stats.issues, { where = where, at = at, message = message })
		end
	end
	if deferred_problems then
		table.insert(deferred_problems, { level = level, where = where, message = message })
	else
		report_problem(level, where, message, where and where.number)
	end
end

local function problem(level, where, fmt, ...)
	problem_at(level, where, nil, fmt, ...)
end

-- What the run spent, for its report: the time preparing karaoke Lines
-- (karaskel), and per template (the template's own table) its time and the
-- lines it generated. Lines a selected-Lines run only goes through are not
-- counted.
local function clock()
	local hikari = aegisub.hikari
	return hikari and hikari.clock() or os.clock()
end

local function spend(template, since, generated)
	if stats and not quiet then
		template.seconds = (template.seconds or 0) + clock() - since
		template.generated = (template.generated or 0) + (generated or 0)
	end
end

-- The Lines the templates apply to, and the Lines generated from them
local function is_karaoke_line(l)
	return l.class == "dialogue" and ((l.effect == "" and not l.comment) or l.effect:match("[Kk]araoke")) and true or false
end

local function is_generated_line(l)
	return l.class == "dialogue" and l.effect == "fx" and not l.comment
end

-- Find and parse/prepare all karaoke template lines
-- (keep_fx: leave the generated lines where they are)
function parse_templates(meta, styles, subs, keep_fx)
	local templates = { once = {}, line = {}, syl = {}, char = {}, furi = {}, styles = {} }
	local i = 1
	local number = 0 -- the Line's number in the Grid, counting the Lines kept so far
	while i <= #subs do
		aegisub.progress.set((i-1) / #subs * 100)
		local l = subs[i]
		i = i + 1
		if l.class == "dialogue" and (keep_fx or not is_generated_line(l)) then
			number = number + 1
		end
		if l.class == "dialogue" and l.comment then
			local fx, mods = string.headtail(l.effect)
			fx = fx:lower()
			-- earlier fx lines are deleted already and nothing is inserted before
			-- this Line later in a whole run, so i-1 is where it stays
			local where = { index = i-1, number = number, kind = fx, effect = l.effect, style = l.style }
			if fx == "code" then
				parse_code(meta, styles, l, templates, mods, where)
			elseif fx == "template" then
				parse_template(meta, styles, l, templates, mods, where)
			end
			templates.styles[l.style] = true
		elseif l.class == "dialogue" and l.effect == "fx" and not keep_fx then
			-- this is a previously generated effect line, remove it
			i = i - 1
			subs.delete(i)
		end
	end
	aegisub.progress.set(100)
	return templates
end

function parse_code(meta, styles, line, templates, mods, where)
	local template = {
		code = line.text,
		loops = 1,
		style = line.style,
		where = where
	}
	local inserted = false

	local rest = mods
	while rest ~= "" do
		local m, t = string.headtail(rest)
		rest = t
		m = m:lower()
		if m == "once" then
			aegisub.debug.out(5, "Found run-once code line: %s\n", line.text)
			table.insert(templates.once, template)
			inserted = true
		elseif m == "line" then
			aegisub.debug.out(5, "Found per-line code line: %s\n", line.text)
			table.insert(templates.line, template)
			inserted = true
		elseif m == "syl" then
			aegisub.debug.out(5, "Found per-syl code line: %s\n", line.text)
			table.insert(templates.syl, template)
			inserted = true
		elseif m == "furi" then
			aegisub.debug.out(5, "Found per-syl code line: %s\n", line.text)
			table.insert(templates.furi, template)
			inserted = true
		elseif m == "all" then
			template.style = nil
		elseif m == "noblank" then
			template.noblank = true
		elseif m == "repeat" or m == "loop" then
			local times, t = string.headtail(rest)
			template.loops = tonumber(times)
			if not template.loops then
				problem(3, where, "Failed reading this repeat-count to a number: %s\nIn template code line: %s\nEffect field: %s\n", times, line.text, line.effect)
				template.loops = 1
			else
				rest = t
			end
		else
			problem(3, where, "Unknown modifier in code template: %s\nIn template code line: %s\nEffect field: %s\n", m, line.text, line.effect)
		end
	end
	
	if not inserted then
		aegisub.debug.out(5, "Found implicit run-once code line: %s\n", line.text)
		table.insert(templates.once, template)
	end
end

-- List of reserved words that can't be used as "line" template identifiers
template_modifiers = {
	"pre-line", "line", "syl", "furi", "char", "all", "repeat", "loop",
	"notext", "keeptags", "noblank", "multi", "fx", "fxgroup"
}

function parse_template(meta, styles, line, templates, mods, where)
	local template = {
		t = "",
		pre = "",
		style = line.style,
		loops = 1,
		layer = line.layer,
		addtext = true,
		keeptags = false,
		fxgroup = nil,
		fx = nil,
		multi = false,
		isline = false,
		perchar = false,
		noblank = false,
		where = where
	}
	local inserted = false
	
	local rest = mods
	while rest ~= "" do
		local m, t = string.headtail(rest)
		rest = t
		m = m:lower()
		if (m == "pre-line" or m == "line") and not inserted then
			aegisub.debug.out(5, "Found line template '%s'\n", line.text)
			-- should really fail if already inserted
			local id, t = string.headtail(rest)
			id = id:lower()
			-- check that it really is an identifier and not a keyword
			for _, kw in pairs(template_modifiers) do
				if id == kw then
					id = nil
					break
				end
			end
			if id == "" then
				id = nil
			end
			if id then
				rest = t
			end
			-- get old template if there is one
			if id and templates.line[id] then
				template = templates.line[id]
			elseif id then
				template.id = id
				templates.line[id] = template
			else
				table.insert(templates.line, template)
			end
			inserted = true
			template.isline = true
			-- apply text to correct string
			-- (a named template joins the text of all its Lines; each Line's
			-- part is kept so a problem can name the Line it is in)
			if m == "line" then
				template.t = template.t .. line.text
				template.t_where = template.t_where or where
				template.t_parts = template.t_parts or {}
				table.insert(template.t_parts, { text = line.text, where = where })
			else -- must be pre-line
				template.pre = template.pre .. line.text
				template.pre_where = template.pre_where or where
				template.pre_parts = template.pre_parts or {}
				table.insert(template.pre_parts, { text = line.text, where = where })
			end
		elseif m == "syl" and not template.isline then
			table.insert(templates.syl, template)
			inserted = true
		elseif m == "furi" and not template.isline then
			table.insert(templates.furi, template)
			inserted = true
		elseif (m == "pre-line" or m == "line") and inserted then
			problem(2, where, "Unable to combine %s class templates with other template classes\n", m)
		elseif (m == "syl" or m == "furi") and template.isline then
			problem(2, where, "Unable to combine %s class template lines with line or pre-line classes\n", m)
		elseif m == "all" then
			template.style = nil
		elseif m == "repeat" or m == "loop" then
			local times, t = string.headtail(rest)
			template.loops = tonumber(times)
			if not template.loops then
				problem(3, where, "Failed reading this repeat-count to a number: %s\nIn template line: %s\nEffect field: %s\n", times, line.text, line.effect)
				template.loops = 1
			else
				rest = t
			end
		elseif m == "notext" then
			template.addtext = false
		elseif m == "keeptags" then
			template.keeptags = true
		elseif m == "multi" then
			template.multi = true
		elseif m == "char" then
			template.perchar = true
		elseif m == "noblank" then
			template.noblank = true
		elseif m == "fx" then
			local fx, t = string.headtail(rest)
			if fx ~= "" then
				template.fx = fx
				rest = t
			else
				problem(3, where, "No fx name following fx modifier\nIn template line: %s\nEffect field: %s\n", line.text, line.effect)
				template.fx = nil
			end
		elseif m == "fxgroup" then
			local fx, t = string.headtail(rest)
			if fx ~= "" then
				template.fxgroup = fx
				rest = t
			else
				problem(3, where, "No fxgroup name following fxgroup modifier\nIn template linee: %s\nEffect field: %s\n", line.text, line.effect)
				template.fxgroup = nil
			end
		else
			problem(3, where, "Unknown modifier in template: %s\nIn template line: %s\nEffect field: %s\n", m, line.text, line.effect)
		end
	end
	
	if not inserted then
		table.insert(templates.syl, template)
	end
	if not template.isline then
		template.t = line.text
	end
end

-- Iterator function, return all templates that apply to the given line
function matching_templates(templates, line, tenv)
	local lastkey = nil
	local function test_next()
		local k, t = next(templates, lastkey)
		lastkey = k
		if not t then
			return nil
		elseif (t.style == line.style or not t.style) and
				(not t.fxgroup or
				(t.fxgroup and tenv.fxgroup[t.fxgroup] ~= false)) then
			return t
		else
			return test_next()
		end
	end
	return test_next
end

-- Iterator function, run a loop using tenv.j and tenv.maxj as loop controllers
function template_loop(tenv, initmaxj)
	local oldmaxj = initmaxj
	tenv.maxj = initmaxj
	tenv.j = 0
	local function itor()
		if tenv.j >= tenv.maxj or aegisub.progress.is_cancelled() then
			return nil
		else
			tenv.j = tenv.j + 1
			if oldmaxj ~= tenv.maxj then
				aegisub.debug.out(5, "Number of loop iterations changed from %d to %d\n", oldmaxj, tenv.maxj)
				oldmaxj = tenv.maxj
			end
			return tenv.j, tenv.maxj
		end
	end
	return itor
end


-- Where generated lines go. A whole run appends them, as 2.1.7 did, and marks
-- each with the karaoke Line it came from (current_source, its index); a
-- selected-Lines run collects them per karaoke Line instead.
local current_source
local function append_generated(subs, newline)
	subs.append(newline)
	if aegisub.hikari then
		-- a mark is never worth failing a run 2.1.7 would complete
		pcall(aegisub.hikari.set_source, #subs, current_source)
	end
end
local emit_line = append_generated

local function collect_generated(into)
	return function(subs, newline)
		local list = into[current_source]
		if not list then
			list = {}
			into[current_source] = list
		end
		table.insert(list, newline)
	end
end

-- Apply the templates (to the karaoke lines at `sources`, ascending indices,
-- when given; with `chosen`, only the lines it holds are changed and report
-- problems)
function apply_templates(meta, styles, subs, templates, sources, chosen)
	-- the environment the templates will run in
	local tenv = {
		meta = meta,
		-- put in some standard libs
		string = string,
		math = math,
		_G = _G
	}
	tenv.tenv = tenv
	
	-- Define helper functions in tenv
	
	tenv.retime = function(mode, addstart, addend)
		local line, syl = tenv.line, tenv.syl
		local newstart, newend = line.start_time, line.end_time
		addstart = addstart or 0
		addend = addend or 0
		if mode == "syl" then
			newstart = line.start_time + syl.start_time + addstart
			newend = line.start_time + syl.end_time + addend
		elseif mode == "presyl" then
			newstart = line.start_time + syl.start_time + addstart
			newend = line.start_time + syl.start_time + addend
		elseif mode == "postsyl" then
			newstart = line.start_time + syl.end_time + addstart
			newend = line.start_time + syl.end_time + addend
		elseif mode == "line" then
			newstart = line.start_time + addstart
			newend = line.end_time + addend
		elseif mode == "preline" then
			newstart = line.start_time + addstart
			newend = line.start_time + addend
		elseif mode == "postline" then
			newstart = line.end_time + addstart
			newend = line.end_time + addend
		elseif mode == "start2syl" then
			newstart = line.start_time + addstart
			newend = line.start_time + syl.start_time + addend
		elseif mode == "syl2end" then
			newstart = line.start_time + syl.end_time + addstart
			newend = line.end_time + addend
		elseif mode == "set" or mode == "abs" then
			newstart = addstart
			newend = addend
		elseif mode == "sylpct" then
			newstart = line.start_time + syl.start_time + addstart*syl.duration/100
			newend = line.start_time + syl.start_time + addend*syl.duration/100
		-- wishlist: something for fade-over effects,
		-- "time between previous line and this" and
		-- "time between this line and next"
		end
		line.start_time = newstart
		line.end_time = newend
		line.duration = newend - newstart
		return ""
	end
	
	tenv.fxgroup = {}
	
	tenv.relayer = function(layer)
		tenv.line.layer = layer
		return ""
	end
	
	tenv.restyle = function(style)
		tenv.line.style = style
		tenv.line.styleref = styles[style]
		return ""
	end
	
	tenv.maxloop = function(newmaxj)
		tenv.maxj = newmaxj
		return ""
	end
	tenv.maxloops = tenv.maxloop
	tenv.loopctl = function(newj, newmaxj)
		tenv.j = newj
		tenv.maxj = newmaxj
		return ""
	end
	
	tenv.recall = {}
	setmetatable(tenv.recall, {
		decorators = {},
		__call = function(tab, name, default)
			local decorator = getmetatable(tab).decorators[name]
			if decorator then
				name = decorator(tostring(name))
			end
			aegisub.debug.out(5, "Recalling '%s'\n", name)
			return tab[name] or default
		end,
		decorator_line = function(name)
			return string.format("_%s_%s", tostring(tenv.orgline), name)
		end,
		decorator_syl = function(name)
			return string.format("_%s_%s", tostring(tenv.syl), name)
		end,
		decorator_basesyl = function(name)
			return string.format("_%s_%s", tostring(tenv.basesyl), name)
		end
	})
	tenv.remember = function(name, value, decorator)
		getmetatable(tenv.recall).decorators[name] = decorator
		if decorator then
			name = decorator(tostring(name))
		end
		aegisub.debug.out(5, "Remembering '%s' as '%s'\n", name, tostring(value))
		tenv.recall[name] = value
		return value
	end
	tenv.remember_line = function(name, value)
		return tenv.remember(name, value, getmetatable(tenv.recall).decorator_line)
	end
	tenv.remember_syl = function(name, value)
		return tenv.remember(name, value, getmetatable(tenv.recall).decorator_syl)
	end
	tenv.remember_basesyl = function(name, value)
		return tenv.remember(name, value, getmetatable(tenv.recall).decorator_basesyl)
	end
	tenv.remember_if = function(name, value, condition, decorator)
		if condition then
			return tenv.remember(name, value, decorator)
		end
		return value
	end
	
	-- run all run-once code snippets
	for k, t in pairs(templates.once) do
		assert(t.code, "WTF, a 'once' template without code?")
		local since = clock()
		run_code_template(t, tenv)
		spend(t, since)
	end
	
	-- start processing lines
	local n = sources and #sources or #subs
	for k = 1, n do
		aegisub.progress.set((k-1)/n*100)
		local i = sources and sources[k] or k
		local l = subs[i]
		if is_karaoke_line(l) then
			current_source = i
			quiet = chosen ~= nil and not chosen[i]
			l.i = i
			l.comment = false
			local since = clock()
			karaskel.preproc_line(subs, meta, styles, l)
			if stats and not quiet then
				stats.prepare = stats.prepare + clock() - since
				stats.lines = stats.lines + 1
			end
			if apply_line(meta, styles, subs, l, templates, tenv) then
				-- Some templates were applied to this line, make a karaoke timing line of it
				l.comment = true
				l.effect = "karaoke"
				if not chosen or chosen[i] then
					subs[i] = l
				end
			end
		end
	end
end

function set_ctx_syl(varctx, line, syl)
	varctx.sstart = syl.start_time
	varctx.send = syl.end_time
	varctx.sdur = syl.duration
	varctx.skdur = syl.duration / 10
	varctx.smid = syl.start_time + syl.duration / 2
	varctx["start"] = varctx.sstart
	varctx["end"] = varctx.send
	varctx.dur = varctx.sdur
	varctx.kdur = varctx.skdur
	varctx.mid = varctx.smid
	varctx.si = syl.i
	varctx.i = varctx.si
	varctx.sleft = math.floor(line.left + syl.left+0.5)
	varctx.scenter = math.floor(line.left + syl.center+0.5)
	varctx.sright = math.floor(line.left + syl.right+0.5)
	varctx.swidth = math.floor(syl.width + 0.5)
	if syl.isfuri then
		varctx.sbottom = varctx.ltop
		varctx.stop = math.floor(varctx.ltop - syl.height + 0.5)
		varctx.smiddle = math.floor(varctx.ltop - syl.height/2 + 0.5)
	else
		varctx.stop = varctx.ltop
		varctx.smiddle = varctx.lmiddle
		varctx.sbottom = varctx.lbottom
	end
	varctx.sheight = syl.height
	if line.halign == "left" then
		varctx.sx = math.floor(line.left + syl.left + 0.5)
	elseif line.halign == "center" then
		varctx.sx = math.floor(line.left + syl.center + 0.5)
	elseif line.halign == "right" then
		varctx.sx = math.floor(line.left + syl.right + 0.5)
	end
	if line.valign == "top" then
		varctx.sy = varctx.stop
	elseif line.valign == "middle" then
		varctx.sy = varctx.smiddle
	elseif line.valign == "bottom" then
		varctx.sy = varctx.sbottom
	end
	varctx.left = varctx.sleft
	varctx.center = varctx.scenter
	varctx.right = varctx.sright
	varctx.width = varctx.swidth
	varctx.top = varctx.stop
	varctx.middle = varctx.smiddle
	varctx.bottom = varctx.sbottom
	varctx.height = varctx.sheight
	varctx.x = varctx.sx
	varctx.y = varctx.sy
end

function apply_line(meta, styles, subs, line, templates, tenv)
	-- Tell whether any templates were applied to this line, needed to know whether the original line should be removed from input
	local applied_templates = false
	
	-- General variable replacement context
	local varctx = {
		layer = line.layer,
		lstart = line.start_time,
		lend = line.end_time,
		ldur = line.duration,
		lmid = line.start_time + line.duration/2,
		style = line.style,
		actor = line.actor,
		margin_l = ((line.margin_l > 0) and line.margin_l) or line.styleref.margin_l,
		margin_r = ((line.margin_r > 0) and line.margin_r) or line.styleref.margin_r,
		margin_t = ((line.margin_t > 0) and line.margin_t) or line.styleref.margin_t,
		margin_b = ((line.margin_b > 0) and line.margin_b) or line.styleref.margin_b,
		margin_v = ((line.margin_t > 0) and line.margin_t) or line.styleref.margin_t,
		syln = line.kara.n,
		li = line.i,
		lleft = math.floor(line.left+0.5),
		lcenter = math.floor(line.left + line.width/2 + 0.5),
		lright = math.floor(line.left + line.width + 0.5),
		lwidth = math.floor(line.width + 0.5),
		ltop = math.floor(line.top + 0.5),
		lmiddle = math.floor(line.middle + 0.5),
		lbottom = math.floor(line.bottom + 0.5),
		lheight = math.floor(line.height + 0.5),
		lx = math.floor(line.x+0.5),
		ly = math.floor(line.y+0.5)
	}
	
	tenv.orgline = line
	tenv.line = nil
	tenv.syl = nil
	tenv.basesyl = nil

	-- Apply all line templates
	aegisub.debug.out(5, "Running line templates\n")
	for t in matching_templates(templates.line, line, tenv) do
		if aegisub.progress.is_cancelled() then break end
		
		-- Set varctx for per-line variables
		varctx["start"] = varctx.lstart
		varctx["end"] = varctx.lend
		varctx.dur = varctx.ldur
		varctx.kdur = math.floor(varctx.dur / 10)
		varctx.mid = varctx.lmid
		varctx.i = varctx.li
		varctx.left = varctx.lleft
		varctx.center = varctx.lcenter
		varctx.right = varctx.lright
		varctx.width = varctx.lwidth
		varctx.top = varctx.ltop
		varctx.middle = varctx.lmiddle
		varctx.bottom = varctx.lbottom
		varctx.height = varctx.lheight
		varctx.x = varctx.lx
		varctx.y = varctx.ly
		
		local since, generated = clock(), 0
		for j, maxj in template_loop(tenv, t.loops) do
			if t.code then
				aegisub.debug.out(5, "Code template, %s\n", t.code)
				tenv.line = line
				-- Although run_code_template also performs template looping this works
				-- by "luck", since by the time the first loop of this outer loop completes
				-- the one run by run_code_template has already performed all iterations
				-- and has tenv.j and tenv.maxj in a loop-ending state, causing the outer
				-- loop to only ever run once.
				run_code_template(t, tenv)
			else
				aegisub.debug.out(5, "Line template, pre = '%s', t = '%s'\n", t.pre, t.t)
				applied_templates = true
				local newline = table.copy(line)
				tenv.line = newline
				newline.layer = t.layer
				newline.text = ""
				if t.pre ~= "" then
					newline.text = newline.text .. run_text_template(t.pre, tenv, varctx, t.pre_where, t.pre_parts)
				end
				if t.t ~= "" then
					for i = 1, line.kara.n do
						local syl = line.kara[i]
						tenv.syl = syl
						tenv.basesyl = syl
						set_ctx_syl(varctx, line, syl)
						newline.text = newline.text .. run_text_template(t.t, tenv, varctx, t.t_where, t.t_parts)
						if t.addtext then
							if t.keeptags then
								newline.text = newline.text .. syl.text
							else
								newline.text = newline.text .. syl.text_stripped
							end
						end
					end
				else
					-- hmm, no main template for the line... put original text in
					if t.keeptags then
						newline.text = newline.text .. line.text
					else
						newline.text = newline.text .. line.text_stripped
					end
				end
				newline.effect = "fx"
				emit_line(subs, newline)
				generated = generated + 1
			end
		end
		spend(t, since, generated)
	end
	aegisub.debug.out(5, "Done running line templates\n\n")
	
	-- Loop over syllables
	for i = 0, line.kara.n do
		if aegisub.progress.is_cancelled() then break end
		local syl = line.kara[i]
		
		aegisub.debug.out(5, "Applying templates to syllable: %s\n", syl.text)
		if apply_syllable_templates(syl, line, templates.syl, tenv, varctx, subs) then
			applied_templates = true
		end
	end
	
	-- Loop over furigana
	for i = 1, line.furi.n do
		if aegisub.progress.is_cancelled() then break end
		local furi = line.furi[i]
		
		aegisub.debug.out(5, "Applying templates to furigana: %s\n", furi.text)
		if apply_syllable_templates(furi, line, templates.furi, tenv, varctx, subs) then
			applied_templates = true
		end
	end
	
	return applied_templates
end

function run_code_template(template, tenv)
	local f, err = loadstring(template.code, "template code")
	if not f then
		problem(2, template.where, "Failed to parse Lua code: %s\nCode that failed to parse: %s\n", err, template.code)
	else
		local pcall = pcall
		setfenv(f, tenv)
		for j, maxj in template_loop(tenv, template.loops) do
			local res, err = pcall(f)
			if not res then
				problem(2, template.where, "Runtime error in template code: %s\nCode producing error: %s\n", err, template.code)
			end
		end
	end
end

-- The Line of a named template's `parts` that the text at `pos` came from,
-- `convert` being what the run did to each part's text so far. A variable or
-- expression across two parts, or no parts, gives the template's first Line.
local function part_where(parts, where, pos, length, convert)
	if not parts or #parts < 2 then
		return where
	end
	local found, at = nil, 0
	for _, part in ipairs(parts) do
		at = at + #convert(part.text)
		if not found and pos <= at then
			found = part.where
		end
	end
	if at ~= length then
		return where
	end
	return found or where
end

-- Where the `length` bytes at `pos` of a template's text are in the text of
-- the Line they came from, for the report (problem_at); nil when they cross
-- two of a named template's Lines.
local function part_at(parts, pos, length, total)
	if not parts or #parts < 2 then
		return { start = pos, length = length }
	end
	local at = 0
	for _, part in ipairs(parts) do
		at = at + #part.text
	end
	if at ~= total then
		return nil
	end
	at = 0
	for _, part in ipairs(parts) do
		if pos <= at + #part.text then
			if pos + length - 1 > at + #part.text then
				return nil
			end
			return { start = pos - at, length = length }
		end
		at = at + #part.text
	end
	return nil
end

function run_text_template(template, tenv, varctx, where, parts)
	local res = template
	aegisub.debug.out(5, "Running text template '%s'\n", res)
	-- What the variables did to each named template part (for part_where)
	local function unchanged(s) return s end
	local replaced = unchanged
	
	-- Replace the variables in the string (this is probably faster than using a custom function, but doesn't provide error reporting)
	if varctx then
		aegisub.debug.out(5, "Has varctx, replacing variables\n")
		local function var_replacer(pos, varname)
			varname = string.lower(varname)
			aegisub.debug.out(5, "Found variable named '%s', ", varname)
			if varctx[varname] ~= nil then
				aegisub.debug.out(5, "it exists, value is '%s'\n", varctx[varname])
				return varctx[varname]
			else
				aegisub.debug.out(5, "doesn't exist\n")
				problem_at(2, part_where(parts, where, pos, #template, unchanged),
					part_at(parts, pos, #varname + 1, #template),
					"Unknown variable name: %s\nIn karaoke template: %s\n", varname, template)
				return "$" .. varname
			end
		end
		res = string.gsub(res, "()$([%a_]+)", var_replacer)
		aegisub.debug.out(5, "Done replacing variables, new template string is '%s'\n", res)
		replaced = function(s)
			return (string.gsub(s, "$([%a_]+)", function(varname)
				varname = string.lower(varname)
				local value = varctx[varname]
				if value ~= nil then
					return value
				end
				return "$" .. varname
			end))
		end
	end
	local expanded = res
	-- Where in the template the character at `pos` of the expanded text was
	-- written; nil inside a variable's value. Worked out only for a problem.
	local function written_at(pos)
		if not varctx then
			return pos
		end
		local from, at = 1, 1
		for start, varname, after in string.gmatch(template, "()$([%a_]+)()") do
			if pos < at + start - from then
				return from + pos - at
			end
			at = at + start - from
			-- var_replacer's result: the value, or the name as written
			local value = varctx[string.lower(varname)]
			local length = value and #tostring(value) or #varname + 1
			if pos < at + length then
				return not value and start + pos - at or nil
			end
			at = at + length
			from = after
		end
		return from + pos - at
	end
	
	-- Function for evaluating expressions
	local function expression_evaluator(pos, expression)
		local function expression_where()
			return part_where(parts, where, pos, #expanded, replaced)
		end
		-- the same expression in the template as written, before the
		-- variables changed where it is; none when a variable's value
		-- brought either of its marks in
		local function expression_at()
			local open = written_at(pos)
			local close = written_at(pos + #expression + 1)
			if open and close then
				return part_at(parts, open, close - open + 1, #template)
			end
		end
		f, err = loadstring(string.format("return (%s)", expression))
		if (err) ~= nil then
			problem_at(2, expression_where(), expression_at(), "Error parsing expression: %s\nExpression producing error: %s\nTemplate with expression: %s\n", err, expression, template)
			return "!" .. expression .. "!"
		else
			setfenv(f, tenv)
			local res, val = pcall(f)
			if res then
				return val
			else
				problem_at(2, expression_where(), expression_at(), "Runtime error in template expression: %s\nExpression producing error: %s\nTemplate with expression: %s\n", val, expression, template)
				return "!" .. expression .. "!"
			end
		end
	end
	-- Find and evaluate expressions
	aegisub.debug.out(5, "Now evaluating expressions\n")
	res = string.gsub(res , "()!(.-)!", expression_evaluator)
	aegisub.debug.out(5, "After evaluation: %s\nDone handling template\n\n", res)
	
	return res
end

function apply_syllable_templates(syl, line, templates, tenv, varctx, subs)
	local applied = 0
	
	-- Loop over all templates matching the line style
	for t in matching_templates(templates, line, tenv) do
		if aegisub.progress.is_cancelled() then break end
		
		tenv.syl = syl
		tenv.basesyl = syl
		set_ctx_syl(varctx, line, syl)
		
		local since = clock()
		local generated = apply_one_syllable_template(syl, line, t, tenv, varctx, subs, false, false)
		spend(t, since, generated)
		applied = applied + generated
	end
	
	return applied > 0
end

function is_syl_blank(syl)
	if syl.duration <= 0 then
		return true
	end
	
	-- try to remove common spacing characters
	local t = syl.text_stripped
	if t:len() <= 0 then return true end
	t = t:gsub("[ \t\n\r]", "") -- regular ASCII space characters
	t = t:gsub("　", "") -- fullwidth space
	return t:len() <= 0
end

function apply_one_syllable_template(syl, line, template, tenv, varctx, subs, skip_perchar, skip_multi)
	if aegisub.progress.is_cancelled() then return 0 end
	local t = template
	local applied = 0
	
	aegisub.debug.out(5, "Applying template to one syllable with text: %s\n", syl.text)
	
	-- Check for right inline_fx
	if t.fx and t.fx ~= syl.inline_fx then
		aegisub.debug.out(5, "Syllable has wrong inline-fx (wanted '%s', got '%s'), skipping.\n", t.fx, syl.inline_fx)
		return 0
	end
	
	if t.noblank and is_syl_blank(syl) then
		aegisub.debug.out(5, "Syllable is blank, skipping.\n")
		return 0
	end
	
	-- Recurse to per-char if required
	if not skip_perchar and t.perchar then
		aegisub.debug.out(5, "Doing per-character effects...\n")
		local charsyl = table.copy(syl)
		tenv.syl = charsyl
		
		local left, width = syl.left, 0
		for c in unicode.chars(syl.text_stripped) do
			charsyl.text = c
			charsyl.text_stripped = c
			charsyl.text_spacestripped = c
			charsyl.prespace, charsyl.postspace = "", "" -- for whatever anyone might use these for
			width = aegisub.text_extents(syl.style, c)
			charsyl.left = left
			charsyl.center = left + width/2
			charsyl.right = left + width
			charsyl.prespacewidth, charsyl.postspacewidth = 0, 0 -- whatever...
			left = left + width
			set_ctx_syl(varctx, line, charsyl)
			
			applied = applied + apply_one_syllable_template(charsyl, line, t, tenv, varctx, subs, true, false)
		end
	
		return applied
	end
	
	-- Recurse to multi-hl if required
	if not skip_multi and t.multi then
		aegisub.debug.out(5, "Doing multi-highlight effects...\n")
		local hlsyl = table.copy(syl)
		tenv.syl = hlsyl
		
		for hl = 1, syl.highlights.n do
			local hldata = syl.highlights[hl]
			hlsyl.start_time = hldata.start_time
			hlsyl.end_time = hldata.end_time
			hlsyl.duration = hldata.duration
			set_ctx_syl(varctx, line, hlsyl)
			
			applied = applied + apply_one_syllable_template(hlsyl, line, t, tenv, varctx, subs, true, true)
		end
	
		return applied
	end

	-- Regular processing
	if t.code then
		aegisub.debug.out(5, "Running code line\n")
		tenv.line = line
		run_code_template(t, tenv)
	else
		aegisub.debug.out(5, "Running %d effect loops\n", t.loops)
		for j, maxj in template_loop(tenv, t.loops) do
			local newline = table.copy(line)
			newline.styleref = syl.style
			newline.style = syl.style.name
			newline.layer = t.layer
			tenv.line = newline
			newline.text = run_text_template(t.t, tenv, varctx, t.where)
			if t.keeptags then
				newline.text = newline.text .. syl.text
			elseif t.addtext then
				newline.text = newline.text .. syl.text_stripped
			end
			newline.effect = "fx"
			aegisub.debug.out(5, "Generated line with text: %s\n", newline.text)
			emit_line(subs, newline)
			applied = applied + 1
		end
	end
	
	return applied
end


-- Main function to do the templating
function filter_apply_templates(subs, config)
	aegisub.progress.task("Collecting header data...")
	local meta, styles = karaskel.collect_head(subs, true)
	
	aegisub.progress.task("Parsing templates...")
	local templates = parse_templates(meta, styles, subs)
	
	aegisub.progress.task("Applying templates...")
	apply_templates(meta, styles, subs, templates)
	return templates
end

-- The template and code Lines that had problems, in order, as a new selection
-- with the first of them active; nothing when there were none.
local function problem_selection()
	local lines = {}
	for index in pairs(problem_lines) do
		table.insert(lines, index)
	end
	if #lines == 0 then
		return
	end
	table.sort(lines)
	return lines, lines[1]
end

-- A run starts clean even when the previous one was cancelled midway.
local function start_run()
	problem_lines = {}
	deferred_problems, quiet = nil, false
	emit_line = append_generated
	stats = { started = clock(), prepare = 0, lines = 0, problems = 0, issues = {}, issue_keys = {} }
end

local function json_string(s)
	return '"' .. (tostring(s):gsub('[%c"\\]', function(c)
		local named = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
		return named[c] or string.format("\\u%04x", c:byte())
	end)) .. '"'
end

local function json_number(x)
	return (string.format("%.6f", x):gsub("0+$", ""):gsub("%.$", ""))
end

-- Attaches the run's report (docs/qt/karaoke-templater.md): what it did and
-- what each template cost, the template Lines numbered as they are after the
-- run (`number_of` maps a template's `where`).
local function report_run(mode, templates, generated, number_of)
	local hikari = aegisub.hikari
	if not hikari or not stats then
		return
	end
	local seen, list = {}, {}
	for _, class in ipairs({ "once", "line", "syl", "char", "furi" }) do
		for _, t in pairs(templates[class]) do
			if t.where and not seen[t] then
				seen[t] = true
				table.insert(list, t)
			end
		end
	end
	table.sort(list, function(a, b) return a.where.index < b.where.index end)
	local entries = {}
	for _, t in ipairs(list) do
		table.insert(entries, string.format('{"line":%d,"effect":%s,"style":%s,"seconds":%s,"generated":%d}',
			number_of(t.where), json_string(t.where.effect), json_string(t.where.style),
			json_number(t.seconds or 0), t.generated or 0))
	end
	local issues = {}
	for _, p in ipairs(stats.issues) do
		table.insert(issues, string.format('{"line":%d,"kind":%s,"message":%s,"start":%d,"length":%d}',
			number_of(p.where), json_string(p.where.kind == "code" and "code" or "template"),
			json_string((p.message:gsub("\n+$", ""))), p.at and p.at.start or 0, p.at and p.at.length or 0))
	end
	hikari.report(string.format('{"report":"karaoke-templater","version":1,"mode":%s,"seconds":%s,' ..
		'"prepare_seconds":%s,"karaoke_lines":%d,"generated":%d,"problems":%d,"templates":[%s],"issues":[%s]}',
		json_string(mode), json_number(clock() - stats.started), json_number(stats.prepare), stats.lines,
		generated, stats.problems, table.concat(entries, ","), table.concat(issues, ",")))
end

function macro_apply_templates(subs, sel)
	start_run()
	local templates = filter_apply_templates(subs, {ismacro=true, sel=sel})
	local generated = 0
	for i = 1, #subs do
		if is_generated_line(subs[i]) then
			generated = generated + 1
		end
	end
	report_run("all", templates, generated, function(where) return where.number end)
	aegisub.set_undo_point("apply karaoke template")
	return problem_selection()
end

function macro_can_template(subs)
	-- check if this file has templates in it, don't allow running the macro if it hasn't
	-- (every Line is looked at: 2.1.7 gave up after the first 50 dialogue lines)
	for i = 1, #subs do
		local l = subs[i]
		-- test if the line is a template
		if l.class == "dialogue" and (string.headtail(l.effect)):lower() == "template" then
			return true
		end
	end
	return false
end


-- Applying the templates to the selected karaoke Lines only (HikariSub).

-- A dialogue line as it reads back once saved: the helper keeps numbers as
-- integers, cut towards zero, and the file loader trims spaces around the
-- actor, effect, text and a TLMode Line's other text.
local function whole(x)
	x = tonumber(x) or 0
	return x >= 0 and math.floor(x) or math.ceil(x)
end

local function trim(s)
	return (s or ""):match("^%s*(.-)%s*$")
end

local function line_key(l)
	return table.concat({ l.comment and "1" or "0", whole(l.layer), whole(l.start_time), whole(l.end_time),
		l.style, trim(l.actor), whole(l.margin_l), whole(l.margin_r), whole(l.margin_t), trim(l.effect),
		trim(l.text), trim(l.text_translation) }, "\0")
end

-- Runs the templates over the karaoke Lines up to `last` in order, as a
-- whole run does (templates can carry state from one Line to the next), and
-- collects each one's generated lines. Only the chosen Lines change.
local function generate(meta, styles, subs, templates, chosen, last)
	local sources, outputs = {}, {}
	for i = 1, last do
		if is_karaoke_line(subs[i]) then
			table.insert(sources, i)
		end
	end
	emit_line = collect_generated(outputs)
	apply_templates(meta, styles, subs, templates, sources, chosen)
	emit_line, quiet = append_generated, false
	return outputs
end

-- The generated lines to replace, ascending: those generated from the chosen
-- karaoke Lines. A generated line whose karaoke Line is unknown (the Document
-- came from a file) is matched against what the templates give the other
-- unmarked karaoke Lines now; the ones no other karaoke Line accounts for are
-- the chosen Lines' (see owned). Also returns the first of those. Nil when
-- another karaoke Line's output is not there as the templates give it now,
-- or a line left over may be another karaoke Line's.
-- The chosen karaoke Lines that may own generated lines nothing else
-- accounts for: those without marked ones, each with the styles its lines
-- can have (its own, and those of its lines now) and, when the other karaoke
-- Lines' generated lines lie in their order as a run leaves them, the part of
-- the file between those of the karaoke Lines before and after it.
local function owners_of(subs, outputs, chosen, known, others, spans)
	local ordered, last = true, 0
	for _, source in ipairs(others) do
		local span = spans[source]
		if span then
			ordered = ordered and span.from > last
			last = span.to
		end
	end
	local owners = {}
	for source in pairs(chosen) do
		if not known[source] then
			local owner = { styles = { [subs[source].style] = true }, after = 0, before = math.huge }
			for _, l in ipairs(outputs[source] or {}) do
				owner.styles[l.style] = true
			end
			if ordered then
				for _, other in ipairs(others) do
					local span = spans[other]
					if span and other < source then
						owner.after = math.max(owner.after, span.to)
					elseif span and other > source then
						owner.before = math.min(owner.before, span.from)
					end
				end
			end
			table.insert(owners, owner)
		end
	end
	return owners
end

-- Whether generated line `l`, at `i`, can be one of the owners'. Otherwise it
-- may be left from another karaoke Line that now generates fewer lines.
local function owned(owners, l, i)
	for _, owner in ipairs(owners) do
		if owner.styles[l.style] and i > owner.after and i < owner.before then
			return true
		end
	end
	return false
end

local function find_generated(subs, outputs, chosen, generated_from, unknown)
	local old, known = {}, {}
	for i = 1, #subs do
		local source = generated_from[i]
		if source and is_generated_line(subs[i]) then
			known[source] = true
			if chosen[source] then
				table.insert(old, i)
			end
		end
	end
	if #unknown == 0 then
		return old
	end
	-- A run generates in karaoke Line order, so identical generated lines
	-- belong to their karaoke Lines in file order: each other karaoke Line, in
	-- order, takes the first of them still free.
	local pool, taken = {}, {}
	for _, i in ipairs(unknown) do
		local key = line_key(subs[i])
		pool[key] = pool[key] or { next = 1 }
		table.insert(pool[key], i)
	end
	local others, spans = {}, {}
	for source in pairs(outputs) do
		if not chosen[source] and not known[source] then
			table.insert(others, source)
		end
	end
	table.sort(others)
	for _, source in ipairs(others) do
		for _, newline in ipairs(outputs[source]) do
			local list = pool[line_key(newline)]
			if not list or list.next > #list then
				return nil
			end
			local at = list[list.next]
			taken[at] = true
			list.next = list.next + 1
			local span = spans[source] or { from = at, to = at }
			span.from, span.to = math.min(span.from, at), math.max(span.to, at)
			spans[source] = span
		end
	end
	local owners = owners_of(subs, outputs, chosen, known, others, spans)
	local first
	for _, i in ipairs(unknown) do
		if not taken[i] then
			if not owned(owners, subs[i], i) then
				return nil
			end
			table.insert(old, i)
			first = math.min(first or i, i)
		end
	end
	table.sort(old)
	return old, first
end

-- The number of entries of the ascending list below x
local function count_below(list, x)
	local lo, hi = 1, #list + 1
	while lo < hi do
		local mid = math.floor((lo + hi) / 2)
		if list[mid] < x then
			lo = mid + 1
		else
			hi = mid
		end
	end
	return lo - 1
end

-- Replaces the old generated lines with the new ones of each karaoke Line:
-- where its first old one was, else where the first one of unknown origin
-- was, else at the end. Marks each new line with its karaoke Line. Returns
-- where a line that stayed is now.
local function replace_generated(subs, sources, outputs, old, first_unknown, generated_from)
	local first_of = {}
	for _, i in ipairs(old) do
		local source = generated_from[i]
		if source and not first_of[source] then
			first_of[source] = i
		end
	end
	local at, anchors, appended = {}, {}, {}
	for _, source in ipairs(sources) do
		local anchor = first_of[source] or first_unknown
		local list = appended
		if anchor then
			if not at[anchor] then
				at[anchor] = {}
				table.insert(anchors, anchor)
			end
			list = at[anchor]
		end
		for _, newline in ipairs(outputs[source] or {}) do
			table.insert(list, { line = newline, source = source })
		end
	end
	table.sort(anchors)
	-- lines inserted before each anchor, cumulated
	local inserted_below = {}
	local total = 0
	for _, anchor in ipairs(anchors) do
		inserted_below[anchor] = total
		total = total + #at[anchor]
	end
	local function now(i)
		local k = count_below(anchors, i)
		local before = k > 0 and inserted_below[anchors[k]] + #at[anchors[k]] or 0
		return i - count_below(old, i) + before
	end
	-- delete runs of old lines from the end, inserting at each anchor
	local hi
	for k = #old, 1, -1 do
		local i = old[k]
		hi = hi or i
		if at[i] or old[k-1] ~= i - 1 then
			subs.deleterange(i, hi)
			local list = at[i]
			for from = 1, list and #list or 0, 200 do
				local chunk = {}
				for j = from, math.min(from + 199, #list) do
					table.insert(chunk, list[j].line)
				end
				subs.insert(i + from - 1, unpack(chunk))
			end
			hi = nil
		end
	end
	for _, entry in ipairs(appended) do
		subs.append(entry.line)
	end
	if aegisub.hikari then
		for _, anchor in ipairs(anchors) do
			local first = anchor - count_below(old, anchor) + inserted_below[anchor]
			for j, entry in ipairs(at[anchor]) do
				aegisub.hikari.set_source(first + j - 1, now(entry.source))
			end
		end
		for j, entry in ipairs(appended) do
			aegisub.hikari.set_source(#subs - #appended + j, now(entry.source))
		end
	end
	return now
end

function macro_apply_templates_to_selection(subs, sel)
	start_run()
	local count = #subs
	aegisub.progress.task("Collecting header data...")
	local meta, styles = karaskel.collect_head(subs, true)
	-- collect_head adds furigana styles, before every dialogue line
	local shift = #subs - count
	aegisub.progress.task("Parsing templates...")
	deferred_problems = {}
	local templates = parse_templates(meta, styles, subs, true)
	local generated_from = aegisub.hikari and aegisub.hikari.sources() or {}
	-- the karaoke Lines selected, or those a selected generated Line came from
	local chosen, sources = {}, {}
	for _, i in ipairs(sel) do
		i = i + shift
		local l = subs[i]
		if is_karaoke_line(l) then
			chosen[i] = true
		elseif is_generated_line(l) and generated_from[i] and is_karaoke_line(subs[generated_from[i]]) then
			chosen[generated_from[i]] = true
		end
	end
	for i in pairs(chosen) do
		table.insert(sources, i)
	end
	table.sort(sources)
	if #sources == 0 then
		aegisub.debug.out(2, "%s\n", tr"Select karaoke lines, or lines generated from them, to apply the template to.")
		aegisub.cancel()
	end
	-- generated lines of unknown origin need every karaoke Line's output
	local unknown = {}
	for i = 1, #subs do
		if is_generated_line(subs[i]) and not generated_from[i] then
			table.insert(unknown, i)
		end
	end
	aegisub.progress.task("Applying templates...")
	local outputs = generate(meta, styles, subs, templates, chosen, #unknown > 0 and #subs or sources[#sources])
	local old, first_unknown = find_generated(subs, outputs, chosen, generated_from, unknown)
	if not old then
		aegisub.debug.out(2, "%s\n", tr"Cannot tell which generated lines came from the selected lines: the other karaoke lines' generated lines are not what the template gives them now (the template or those lines changed, or the template uses random values). Apply the template to the whole file once; after that, selected lines can be applied to on their own.")
		aegisub.cancel()
	end
	local now = replace_generated(subs, sources, outputs, old, first_unknown, generated_from)
	-- the problems, with the numbers the Lines have now
	local problems = deferred_problems
	deferred_problems = nil
	for _, p in ipairs(problems) do
		report_problem(p.level, p.where, p.message, p.where and now(p.where.index) - (p.where.index - p.where.number))
	end
	local moved = {}
	for index in pairs(problem_lines) do
		moved[now(index)] = true
	end
	problem_lines = moved
	local generated = 0
	for _, source in ipairs(sources) do
		generated = generated + #(outputs[source] or {})
	end
	report_run("selected", templates, generated, function(where)
		return now(where.index) - (where.index - where.number)
	end)
	aegisub.set_undo_point("apply karaoke template")
	return problem_selection()
end

function macro_can_template_selection(subs, sel)
	if not macro_can_template(subs) then
		return false
	end
	for _, i in ipairs(sel) do
		local l = subs[i]
		if is_karaoke_line(l) or is_generated_line(l) then
			return true
		end
	end
	return false
end

aegisub.register_macro(tr"Apply karaoke template", tr"Applies karaoke effects from templates", macro_apply_templates, macro_can_template)
aegisub.register_macro(tr"Apply karaoke template to selected lines", tr"Applies karaoke effects from templates to the selected lines only, replacing their earlier generated lines", macro_apply_templates_to_selection, macro_can_template_selection)
aegisub.register_filter(tr"Karaoke template", tr"Apply karaoke effect templates to the subtitles.\n\nSee the help file for information on how to use this.", 2000, filter_apply_templates)
