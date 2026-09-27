-- Pet Companion's rules in Lua: the same orders the buttons give
-- (order.* and the Ordered / OrderDone events, docs/COMMANDS.md).
-- Save this file while the game runs and it reloads.

local fetches = 0

hook.Add("OrderDone", "praise", function(e)
    if e.order == "fetch" and e.ok then
        fetches = fetches + 1
        print("fetches so far: " .. fetches)
        -- Every third fetch the dog has earned a rest: sit.
        if fetches % 3 == 0 then order.sit(e.unit) end
    end
end)

hook.Add("Ordered", "log", function(e)
    print("the dog was told: " .. e.order)
end)
