-- Platoon's rules in Lua: the same orders the buttons give
-- (order.* and the Ordered / OrderDone events, docs/COMMANDS.md).
-- Save this file while the game runs and it reloads.

-- Report in when a soldier reaches the spot they were sent to.
hook.Add("OrderDone", "in position", function(e)
    if e.order == "move" and e.ok then print("soldier " .. e.unit .. " in position") end
end)

-- Every focus-fire order, once per soldier who got it.
hook.Add("Ordered", "focus", function(e)
    if e.order == "focus" then print("soldier " .. e.unit .. ": focus fire on " .. tostring(e.target)) end
end)
