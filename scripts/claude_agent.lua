-- @Title: Claude AI Agent
-- @Description: Chat with Claude AI directly from the map editor
-- @Author: VLL Systems
-- @Version: 1.0.0
-- ============================================================================
-- STORAGE (persistent API key)
-- ============================================================================
local storage = app.storage("claude_config")
local config = storage:load() or {}

-- ============================================================================
-- API KEY SETUP
-- ============================================================================

if not config.apiKey or config.apiKey == "" then
	local keyDlg = Dialog({
		title = "Claude AI - Setup",
		width = 450,
	})
	keyDlg:label({
		text = "Enter your Anthropic API key to get started.",
	})
	keyDlg:label({
		text = "Get one at: https://console.anthropic.com/settings/keys",
	})
	keyDlg:separator()
	keyDlg:input({
		id = "apiKey",
		label = "API Key:",
		text = "",
		focus = true,
	})
	keyDlg:newrow()
	keyDlg:combobox({
		id = "model",
		label = "Model:",
		options = {
			"claude-sonnet-4-20250514",
			"claude-haiku-4-20250414",
			"claude-3-5-sonnet-20241022",
			"claude-3-5-haiku-20241022",
		},
		option = "claude-sonnet-4-20250514",
	})
	keyDlg:newrow()
	keyDlg:separator()
	keyDlg:button({
		id = "save",
		text = "Save & Continue",
		focus = true,
		onclick = function(d)
			d:close()
		end,
	})
	keyDlg:show()

	local keyData = keyDlg.data
	if not keyData.apiKey or keyData.apiKey == "" then
		print("[Claude] No API key provided. Aborting.")
		return
	end

	config.apiKey = keyData.apiKey
	config.model = keyData.model or "claude-sonnet-4-20250514"
	storage:save(config)
	print("[Claude] API key saved.")
end

-- ============================================================================
-- CONVERSATION HISTORY
-- ============================================================================

local conversationHistory = {}
local systemPrompt = [[You are an AI assistant integrated into Remere's Map Editor (a Tibia map editor).  
You help users with map editing tasks, Lua scripting for the editor, and general Tibia mapping questions.  
Keep responses concise and practical. When suggesting code, use the editor's Lua API.  
Available APIs: app, map, tile, Items, Brushes, algo, noise, geo, http, json, Dialog.  
The user can run Lua scripts to manipulate the map programmatically.]]

-- ============================================================================
-- HELPER: Parse SSE stream from Claude
-- ============================================================================

local function parseSSEChunks(rawData)
	local texts = {}
	-- Split by lines and look for data: lines with content_block_delta
	for line in rawData:gmatch("[^\r\n]+") do
		if line:sub(1, 6) == "data: " then
			local jsonStr = line:sub(7)
			if jsonStr ~= "[DONE]" then
				local ok, parsed = pcall(json.decode, jsonStr)
				if ok and parsed then
					if
						parsed.type == "content_block_delta"
						and parsed.delta
						and parsed.delta.type == "text_delta"
						and parsed.delta.text
					then
						table.insert(texts, parsed.delta.text)
					end
				end
			end
		end
	end
	return table.concat(texts, "")
end

-- ============================================================================
-- HELPER: Send message to Claude (streaming)
-- ============================================================================

local function sendToClaude(userMessage)
	-- Add user message to history
	table.insert(conversationHistory, {
		role = "user",
		content = userMessage,
	})

	-- Build request body
	local body = {
		model = config.model or "claude-sonnet-4-20250514",
		max_tokens = 2048,
		system = systemPrompt,
		stream = true,
		messages = conversationHistory,
	}

	local headers = {
		["x-api-key"] = config.apiKey,
		["anthropic-version"] = "2023-06-01",
	}

	print("[Claude] Sending message...")

	-- Start streaming request
	local streamResult = http.postJsonStream("https://api.anthropic.com/v1/messages", body, headers)

	if not streamResult.ok then
		print("[Claude] ERROR: Failed to start request: " .. tostring(streamResult.error))
		return nil, streamResult.error
	end

	local sessionId = streamResult.sessionId
	local fullResponse = ""
	local lastPrintLen = 0

	-- Poll for streaming data
	while true do
		app.sleep(100)
		app.yield()

		local readResult = http.streamRead(sessionId)

		if readResult.data and readResult.data ~= "" then
			local newText = parseSSEChunks(readResult.data)
			if newText ~= "" then
				fullResponse = fullResponse .. newText
				-- Print new content incrementally (show progress in console)
				local newPart = fullResponse:sub(lastPrintLen + 1)
				if #newPart > 0 then
					-- Print in chunks to show progress
					io.write(newPart)
					lastPrintLen = #fullResponse
				end
			end
		end

		if readResult.hasError then
			print("\n[Claude] Stream error: " .. tostring(readResult.error))
			http.streamClose(sessionId)
			return nil, readResult.error
		end

		if readResult.finished then
			break
		end
	end

	http.streamClose(sessionId)
	print("") -- newline after streaming output

	if fullResponse == "" then
		print("[Claude] WARNING: Empty response received.")
		return nil, "Empty response"
	end

	-- Add assistant response to history
	table.insert(conversationHistory, {
		role = "assistant",
		content = fullResponse,
	})

	return fullResponse, nil
end

-- ============================================================================
-- CHAT LOOP
-- ============================================================================

print("[Claude] Agent ready. Model: " .. (config.model or "claude-sonnet-4-20250514"))
print("[Claude] Conversation history will be maintained during this session.")
print("")

local keepChatting = true

while keepChatting do
	-- Build chat dialog
	local chatDlg = Dialog({
		title = "Claude AI Chat",
		width = 500,
	})

	-- Show conversation summary if we have history
	if #conversationHistory > 0 then
		local lastMsg = conversationHistory[#conversationHistory]
		local preview = lastMsg.content:sub(1, 200)
		if #lastMsg.content > 200 then
			preview = preview .. "..."
		end
		chatDlg:label({
			text = "Last response (" .. #conversationHistory .. " messages):",
		})
		chatDlg:label({
			id = "preview",
			text = preview,
		})
		chatDlg:separator()
	end

	chatDlg:label({
		text = "Type your message:",
	})
	chatDlg:newrow()
	chatDlg:input({
		id = "message",
		label = "",
		text = "",
		focus = true,
	})
	chatDlg:newrow()

	chatDlg:separator()

	chatDlg:combobox({
		id = "model",
		label = "Model:",
		options = {
			"claude-sonnet-4-20250514",
			"claude-haiku-4-20250414",
			"claude-3-5-sonnet-20241022",
			"claude-3-5-haiku-20241022",
		},
		option = config.model or "claude-sonnet-4-20250514",
	})
	chatDlg:newrow()

	chatDlg:button({
		id = "send",
		text = "Send",
		focus = true,
		onclick = function(d)
			d:close()
		end,
	})
	chatDlg:button({
		id = "clear",
		text = "Clear History",
		onclick = function(d)
			conversationHistory = {}
			print("[Claude] Conversation history cleared.")
			d:close()
		end,
	})
	chatDlg:button({
		id = "settings",
		text = "Change API Key",
		onclick = function(d)
			config.apiKey = ""
			storage:save(config)
			print("[Claude] API key cleared. Restart the script to set a new one.")
			keepChatting = false
			d:close()
		end,
	})
	chatDlg:button({
		id = "quit",
		text = "Close",
		onclick = function(d)
			keepChatting = false
			d:close()
		end,
	})

	chatDlg:show()

	local chatData = chatDlg.data

	-- Update model if changed
	if chatData.model and chatData.model ~= config.model then
		config.model = chatData.model
		storage:save(config)
		print("[Claude] Model changed to: " .. config.model)
	end

	-- Check if user wants to quit
	if not keepChatting then
		break
	end

	-- Check if clear was pressed (no message to send)
	if chatData.clear then
		-- History already cleared in onclick
		goto continue_loop
	end

	-- Check if there's a message to send
	local message = chatData.message
	if not message or message == "" then
		break
	end

	-- Add map context if available
	if app.hasMap() then
		local map = app.map
		local mapContext =
			string.format('\n[Map context: "%s", %dx%d tiles]', map.name or "unnamed", map.width or 0, map.height or 0)
		-- Only add context to first message
		if #conversationHistory == 0 then
			message = message .. mapContext
		end
	end

	print(string.format("\n[You] %s\n", message))
	print("[Claude] ...")

	local response, err = sendToClaude(message)

	if response then
		print(string.format("\n[Claude] Response complete (%d chars)", #response))

		-- Show response in alert for easy reading/copying
		app.alert({
			title = "Claude Response",
			text = response,
			buttons = { "OK" },
		})
	else
		print("[Claude] Failed: " .. tostring(err))
		-- Remove the failed user message from history
		if #conversationHistory > 0 and conversationHistory[#conversationHistory].role == "user" then
			table.remove(conversationHistory)
		end
	end

	::continue_loop::
end

print("[Claude] Chat session ended.")
