Welcome to the spaceship a roguelike game, we continue our work on the C++ port, as always.

STRICT RULE:
Do not invent. Do not polish. Do not add guards, state machines, safety rails, edge-case resets, or save/load
handoffs.

IMPORTANT: 
All state machines and gates enforcing anti-roguelike logic are FORBIDDEN. 
If you think "what if X breaks," leave it. Roguelikes live in edge
states; the player is allowed to discover and live with consequences. 
Do not silently reset flags on arrival, on save, on death, on dock, or on any other
lifecycle event unless the existing code already does so for the same kind of flag. 
Do not add any new tier, new charge key alias, new constants, or new
helpers.
-----------------------------------------------------------------------

## Beta status: 
The game is in active development. Existing mechanics, screens, text, and controls describe the current implementation, 
not a final design. They may change at the owner’s direction. Do not turn tentative ideas 
into requirements or add unrequested behavior such as automatic loading. 
-----------------------------------------------------------------------

## Antipattern-Block:
- „Do NOT add state machines for worker behavior."
- „Do NOT create event systems for button presses."
- „Do NOT interpret or 'improve' the gameplay logic."

## SOURCE AUTHORITY: ABSOLUTE; NO EXCEPTIONS
1. the owner's instructions
2. tools/viewer
3. cpp/

## HARD RULES
-**Do not edit files without the owner's permission.** Wait for explicit approval from the repository owner
   before creating, changing, moving or deleting any file. If permission is unclear (LOGBOOK anti-roguelike),
   ask and stop; never make the edit first and explain afterwards.  
- **Do not hide differences.** Do not silently omit content, truncate values, inert a
   control, change a cost, or shrink text to avoid overflow.
  
## Agent Comprehension Check (Q&A)
**CRITICAL INSTRUCTION:** 
At the beginning of a new session, after reading `AGENTS.md`, you must confirm the BETA status and answer the following questions. 
Do not write any code or perform any tasks until you have output the answers:
1. What happens when you assume the code without verification?
2. Explain the problem with "commentary in code"?
3. Why no anti pattern?
4. Why is the player allowed to discover and live with consequences?
