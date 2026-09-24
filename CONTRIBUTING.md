# Contributing to DolRecomp

Well... Let me just start over...

## AI guidelines

As of Early-August 2026 we have been accepting some AI contributions (**albiet controlled in areas we want it to be**). "**AI**" in this case means a Large Language Model ("**LLM**"), such as **ChatGPT**, **Claude**, **Copilot**, **Grok**, **etc**, but that does not automatically mean any AI generated PRs can be accepted. If your PR uses some form of generative AI for specific changes or additions (Whether code or documentation), they must follow these specific-guidelines

1. The following PR has been tested and verified to compile and run as intended.
2. The following code does not contain any over-explained comments for simple-single line additions. (A clear example of this can be found [here](https://github.com/mstan/PokemonStadiumRecomp/blob/main/game.toml))
3. The following changes do not break GitHub workflows. (This is to make sure our code can compile on all 3 target platforms, Windows, macOS, and Linux, either through the **C** backend or the **LLVM** backend)

Any AI generated PRs that do not follow guidelines will be considered "**slop**" and will immediately be rejected.

Remember. If your PR causes a workflow break, makes the code uncompilable, or destroy someone's computer, then that's on **you** because you are entirely responsible for what your PR does.

## But now...

Other than that, the standard guidelines for human-made PRs are pretty much the same as the ones we have for AI PRs so... There's not much else to add here. Just, follow the rules, and stay cool. Okay?

<sub>Also the old CONTRIBUTING.md was written entirely using AI, so after the new rule changes I have completely rewritten all of this entirely by myself :D
