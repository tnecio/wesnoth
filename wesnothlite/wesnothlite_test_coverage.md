# Common setup

- Custom unit:
  - TestAdept
    - inherits from DarkAdept
    - has 100% defence in forest
    - has 0% defence everywhere else

- Map:
  - includes two keeps with castles, and a bit of grass and a bit of forest in between

# Test scenarios

## basic

Setup:
- short `story` before scenario 1
- two scenarios with the same map, objective to kill enemy leader
- starting with one leader, objective is to kill the enemy leader (TestingAdept who has 1 HP)

Sequence:
- Scenario 1
    - First we see some narration and objectives
    - Leader recruits two units
        - Two units spawn in keep
    - Leader is instructed to kill the enemy leader (TestingAdept)
        - Leader moves next to the enemy leader, kills him, gains 8 XP and advances to the next scenario
- Scenario 2
    - Leader recalls his two units
        - Two units spawn in the keep
    - Leader is instructed to kill the enemy leader (TestingAdept)
        - Leader moves next to the enemy leader, kills him, gains 8 XP and the campaign finishes with victory

