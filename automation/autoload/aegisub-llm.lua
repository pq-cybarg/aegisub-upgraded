--[[
  Aegisub LLM automation macros.

  This is the scriptable / automation-facing half of Aegisub's LLM features. It
  mirrors the built-in LLM menu (Subtitle ▸ ... and the LLM menu) but runs as an
  Automation 4 Lua script, so it works in any build and can be invoked from the
  Automation menu, bound to hotkeys, or adapted by users.

  It shells out to `curl` (present on macOS and most Linux installs) and shares
  the same configuration keys that the native feature stores, read here from the
  environment for portability:

      AEGISUB_LLM_PROVIDER   anthropic | openai   (default: anthropic)
      AEGISUB_LLM_ENDPOINT   base URL             (default: provider default)
      AEGISUB_LLM_MODEL      model name           (default: claude-sonnet-4-6)
      AEGISUB_LLM_KEY        API key              (falls back to
                             ANTHROPIC_API_KEY / OPENAI_API_KEY)
      AEGISUB_LLM_TARGET_LANG target language     (default: English)

  Local models work out of the box by pointing the endpoint at Ollama
  (http://localhost:11434/v1) or llama.cpp's server (http://localhost:8080/v1)
  with provider = openai.
]]

script_name = "LLM tools"
script_description = "Translate, proofread and rewrite subtitles with an LLM"
script_author = "Aegisub LLM contributors"
script_version = "1.0.0"

local function getenv_default(name, default)
	local v = os.getenv(name)
	if v == nil or v == "" then return default end
	return v
end

local function config()
	local provider = getenv_default("AEGISUB_LLM_PROVIDER", "anthropic"):lower()
	local cfg = {
		provider = provider,
		model = getenv_default("AEGISUB_LLM_MODEL", "claude-sonnet-4-6"),
		key = getenv_default("AEGISUB_LLM_KEY",
			provider == "openai" and getenv_default("OPENAI_API_KEY", "")
			                      or getenv_default("ANTHROPIC_API_KEY", "")),
		target_lang = getenv_default("AEGISUB_LLM_TARGET_LANG", "English"),
	}
	if provider == "openai" then
		cfg.endpoint = getenv_default("AEGISUB_LLM_ENDPOINT", "https://api.openai.com/v1")
	else
		cfg.endpoint = getenv_default("AEGISUB_LLM_ENDPOINT", "https://api.anthropic.com")
	end
	return cfg
end

-- Minimal JSON string escaper for request bodies.
local function json_escape(s)
	s = s:gsub("\\", "\\\\")
	s = s:gsub('"', '\\"')
	s = s:gsub("\n", "\\n")
	s = s:gsub("\r", "\\r")
	s = s:gsub("\t", "\\t")
	return s
end

-- Write a string to a temporary file and return its path. The file is created
-- with owner-only permissions because it may hold the API key (curl config).
math.randomseed(os.time())
local function write_temp(contents)
	local path = aegisub.decode_path("?temp") .. "/aegisub_llm_" .. tostring(os.time()) ..
		"_" .. tostring(math.random(1, 2147483647)) .. ".tmp"
	local f = assert(io.open(path, "w"))
	f:write(contents)
	f:close()
	os.execute('chmod 600 "' .. path .. '" 2>/dev/null')
	return path
end

-- Escape a value for use inside a double-quoted curl config-file entry.
local function curl_escape(s)
	return (s:gsub('\\', '\\\\'):gsub('"', '\\"'))
end

-- Run a single completion and return the assistant's text (or nil, error).
local function complete(cfg, system_prompt, user_prompt)
	local body
	if cfg.provider == "openai" then
		body = string.format(
			'{"model":"%s","temperature":0.3,"max_tokens":4096,"messages":[' ..
			'{"role":"system","content":"%s"},{"role":"user","content":"%s"}]}',
			json_escape(cfg.model), json_escape(system_prompt), json_escape(user_prompt))
	else
		body = string.format(
			'{"model":"%s","max_tokens":4096,"temperature":0.3,"system":"%s",' ..
			'"messages":[{"role":"user","content":"%s"}]}',
			json_escape(cfg.model), json_escape(system_prompt), json_escape(user_prompt))
	end

	local body_file = write_temp(body)

	-- Build a curl config file (-K) so the API key and URL never appear in the
	-- process argument list (visible via `ps`) and are never interpreted by the
	-- shell. Only the random temp path is interpolated into the command.
	local url, auth_lines
	if cfg.provider == "openai" then
		url = cfg.endpoint .. "/chat/completions"
		auth_lines = cfg.key ~= ""
			and ('header = "Authorization: Bearer ' .. curl_escape(cfg.key) .. '"\n') or ""
	else
		url = cfg.endpoint .. "/v1/messages"
		auth_lines = 'header = "x-api-key: ' .. curl_escape(cfg.key) .. '"\n' ..
			'header = "anthropic-version: 2023-06-01"\n'
	end

	local conf = "silent\nshow-error\nrequest = \"POST\"\n" ..
		'url = "' .. curl_escape(url) .. '"\n' ..
		'header = "Content-Type: application/json"\n' ..
		auth_lines ..
		'data-binary = "@' .. curl_escape(body_file) .. '"\n'
	local conf_file = write_temp(conf)

	local pipe = io.popen('curl -K "' .. conf_file .. '"')
	local response = pipe:read("*a")
	pipe:close()
	os.remove(body_file)
	os.remove(conf_file)

	-- Extract the assistant text from either provider's response shape. We keep
	-- this tolerant rather than pulling in a full JSON parser.
	local text
	if cfg.provider == "openai" then
		text = response:match('"content"%s*:%s*"(.-[^\\])"')
	else
		text = response:match('"text"%s*:%s*"(.-[^\\])"')
	end
	if not text then
		local err = response:match('"message"%s*:%s*"(.-[^\\])"')
		return nil, err or ("Unexpected response: " .. response:sub(1, 300))
	end
	-- Unescape the JSON string.
	text = text:gsub('\\n', '\n'):gsub('\\t', '\t'):gsub('\\"', '"'):gsub('\\\\', '\\')
	return text
end

-- Apply an operation to every selected line, one request each.
local function apply_to_selection(subs, sel, system_prompt, make_instruction)
	local cfg = config()
	if cfg.key == "" and cfg.endpoint:find("localhost") == nil then
		aegisub.log("No API key set. Set AEGISUB_LLM_KEY or ANTHROPIC_API_KEY / OPENAI_API_KEY.\n")
		return
	end

	for i, idx in ipairs(sel) do
		if aegisub.progress.is_cancelled() then break end
		aegisub.progress.set(((i - 1) / #sel) * 100)
		aegisub.progress.task(string.format("Line %d / %d", i, #sel))

		local line = subs[idx]
		local prompt = make_instruction(line) .. "\n\n" ..
			"Return ONLY the resulting line text with no commentary. Preserve every " ..
			"ASS override tag (text in braces like {\\i1}) and every \\N break exactly.\n\n" ..
			"Input line:\n" .. line.text
		local result, err = complete(cfg, system_prompt, prompt)
		if result then
			line.text = result:gsub("^%s+", ""):gsub("%s+$", "")
			subs[idx] = line
		else
			aegisub.log("Error on line %d: %s\n", i, tostring(err))
		end
	end
	aegisub.set_undo_point(script_name)
end

local function macro_translate(subs, sel)
	local cfg = config()
	apply_to_selection(subs, sel,
		"You are an expert subtitle translator.",
		function() return "Translate this subtitle line into " .. cfg.target_lang .. "." end)
end

local function macro_proofread(subs, sel)
	apply_to_selection(subs, sel,
		"You are a meticulous subtitle proofreader.",
		function() return "Fix spelling, grammar and punctuation in this line without changing its meaning. Do not translate." end)
end

local function macro_rephrase(subs, sel)
	apply_to_selection(subs, sel,
		"You turn stiff or machine-translated dialogue into natural speech.",
		function() return "Rewrite this line as natural spoken dialogue with the same meaning. Do not translate." end)
end

local function can_run(subs, sel)
	return sel ~= nil and #sel > 0
end

aegisub.register_macro("LLM/Translate selected lines", "Translate the selected lines", macro_translate, can_run)
aegisub.register_macro("LLM/Proofread selected lines", "Proofread the selected lines", macro_proofread, can_run)
aegisub.register_macro("LLM/Rephrase selected lines", "Rephrase the selected lines", macro_rephrase, can_run)
