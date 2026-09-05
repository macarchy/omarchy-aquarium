# Changelog

## [0.3.1](https://github.com/macarchy/omarchy-aquarium/compare/v0.3.0...v0.3.1) (2026-09-05)


### Bug Fixes

* **ci:** let pacman work inside the Arch ARM container ([#12](https://github.com/macarchy/omarchy-aquarium/issues/12)) ([6052146](https://github.com/macarchy/omarchy-aquarium/commit/60521465c95b9ad7ae705ecc1942bd6ab5cd7da0))

## [0.3.0](https://github.com/macarchy/omarchy-aquarium/compare/v0.2.1...v0.3.0) (2026-09-05)


### Features

* **pkg:** publish an aarch64 package on every release ([#10](https://github.com/macarchy/omarchy-aquarium/issues/10)) ([fae19e3](https://github.com/macarchy/omarchy-aquarium/commit/fae19e34f03e8ff62aa5c7a9dc099ebdf710c670))

## [0.2.1](https://github.com/macarchy/omarchy-aquarium/compare/v0.2.0...v0.2.1) (2026-09-04)


### Bug Fixes

* **toggle:** tell the desktop when the tank goes on or off ([#6](https://github.com/macarchy/omarchy-aquarium/issues/6)) ([e1799bb](https://github.com/macarchy/omarchy-aquarium/commit/e1799bb76fa6d613f29b848f89237d5e6dfdb5bc))

## [0.2.0](https://github.com/macarchy/omarchy-aquarium/compare/v0.1.0...v0.2.0) (2026-09-04)


### Features

* **bench:** scored eval harness for autonomous optimisation ([dbe4a49](https://github.com/macarchy/omarchy-aquarium/commit/dbe4a49bdb5b1a10af70443fa4c2b221086a4a5c))
* **install:** wire the keybind and the login start on any Omarchy ([#5](https://github.com/macarchy/omarchy-aquarium/issues/5)) ([59c4b8e](https://github.com/macarchy/omarchy-aquarium/commit/59c4b8e42e500839789e9616881b72775fed2fff))
* **react:** cursor scatter, notification startle, night bioluminescence ([abf82f2](https://github.com/macarchy/omarchy-aquarium/commit/abf82f2381b753d60580034e029b923c115f2de1))


### Bug Fixes

* **bench:** score a paired A/B, not an absolute against a recorded baseline ([9570028](https://github.com/macarchy/omarchy-aquarium/commit/957002872832f3b44c4c2cd96a6e8ab93ded72f6))
* **bench:** score a paired A/B, not an absolute against a recorded baseline ([4e4d081](https://github.com/macarchy/omarchy-aquarium/commit/4e4d0813566542f43ea58c923d25f6c0e724f60b))
* **react:** tune the scatter subtle and fish-like ([2bf87b6](https://github.com/macarchy/omarchy-aquarium/commit/2bf87b6d98920e55d7941af4c095babe00d1eea5))
* **toggle:** put the tank back at login instead of losing it on reboot ([#4](https://github.com/macarchy/omarchy-aquarium/issues/4)) ([157d3af](https://github.com/macarchy/omarchy-aquarium/commit/157d3af0f64be6b93b014b7eefa3b19e2ffe54fb))


### Performance Improvements

* **floor:** anemones and starfish — bound the disc by what the mask can reach ([8746088](https://github.com/macarchy/omarchy-aquarium/commit/8746088bfdd76b9232755a6cba67052d8af13cf3))
* **floor:** bound anemone and starfish discs by their true reach (1.03x) ([60b2389](https://github.com/macarchy/omarchy-aquarium/commit/60b2389ebdfa26543dfe9e463526cb469e3cecfc))
* **floor:** gate the seated creatures by their true reach (1.006-1.014x, bit-identical) ([ace77fd](https://github.com/macarchy/omarchy-aquarium/commit/ace77fd11b5fff8922f853ad68a30bf16f22650f))
* **floor:** gate the seated creatures by what they can reach ([8d959d9](https://github.com/macarchy/omarchy-aquarium/commit/8d959d9e784b2025e3d09ffa30a778e068e7e556))
* **seaweed:** bound a blade by its reach, not by the sum of its maxima ([5c5acef](https://github.com/macarchy/omarchy-aquarium/commit/5c5acef49f8b1b747999b3e810375f4cd4872c4d))
* **seaweed:** tighten the blade bound to its true envelope (1.01-1.02x) ([c144872](https://github.com/macarchy/omarchy-aquarium/commit/c1448722a83c8b65b4701f31940522fa3f7147be))
* **shader:** bake the smooth layers into a small background target ([9089c37](https://github.com/macarchy/omarchy-aquarium/commit/9089c3707830562bfdab64aed5d372cecee98d2b))
* **shader:** declare precision instead of paying for highp everywhere ([3efd966](https://github.com/macarchy/omarchy-aquarium/commit/3efd966a086498ea8c5f4a941bf052111d2b59a0))
* **shader:** half-frequency background target (1.22x dev, 1.20x held-out) ([1e81261](https://github.com/macarchy/omarchy-aquarium/commit/1e81261573bfd1322ad32f81e3c5ac22f7aa8adc))
* **shader:** precision qualifiers, mediump colour path (1.024x) ([b10d7f9](https://github.com/macarchy/omarchy-aquarium/commit/b10d7f9990843fffb23e649f84411224cff3b9ee))
