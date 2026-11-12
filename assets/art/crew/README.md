# Crew artwork

Pictures for the Crew Quarters. The client loads every `.png` in this folder at start and finds a picture by its file name. `<id>` is the crew id in `assets/data/crew.json`.

| File | Where it is painted | Pixel size |
| --- | --- | --- |
| `<id>-portrait.png` | The card left of the corridor. Head and shoulders, cut off at the lower edge. | 464 x 620 |
| `<id>-stand.png` | The figure in the corridor: whole body, facing the viewer. | 276 to 320 high plus a 2 px margin |
| `<id>-stand-cloth.png` | The uniform of that figure, painted under it in the colour of the job. | same as the figure |

Every picture has a transparent background.

**Portrait.** The card is 232 x 310 at the 18 px reading size and grows with the reading size. The whole picture is fitted into it and stands on its lower edge, so the files are twice the card. Name and role are written on a plate over the lowest sixth.

**Figure.** Painted at half its pixel size at the 18 px reading size, the middle of its lower edge on the mark before the door. The heights differ so that the bodies are one size: a high bun adds to the picture, a shaved head takes from it.

**Uniform layer.** The figure file is transparent where the uniform is. The `-cloth` file holds the uniform alone in grey with its shading; the client multiplies it with the job's `cloth` colour from `crew.json` and paints the figure over it. A figure without a `-cloth` file is painted as it is and does not change colour with the job.

**Poses.** The client asks for `<id>-stand` when the hand has a job or is at the bench, for `<id>-sit` and then `<id>-stand` when the hand has no job, and for `<id>-lie` when the hand is sent to the doc. A `lie` picture has to bring its own cot. No `sit` and no `lie` picture is in this folder.

**Without a picture.** A hand with no figure file is painted from the sprite tables in `assets/data/crew-art.json`, as before. A hand with no portrait file gets an empty card that says so.

## What is here

A portrait and a standing figure with its uniform layer for each of the nine hands in `crew.json`: `rell`, `jax`, `bo`, `suri`, `nemi`, `ori`, `ryn`, `sol`, `umi`.

All of them were made with an image generator on 2026-11-11. The portraits come from descriptions written out of `crew.json`: hair, skin, age, role, and one thing from the `hobby` line (the pencil, the goggles, the flower clip, the headset, the loupe, the rolled chart, the headphones). Bo's glasses and moustache and Ori's beard are not in the data, and Nemi's hair came out redder than her `hair` colour. Each figure was generated from its portrait, with the figures of `rell` and `jax` as the model for the style. None is drawn by hand and none was supplied by the owner. Each was cut out of its white background and reduced to the sizes above; for the figures, the green jumpsuit of the generated picture was separated into the `-cloth` layer and turned grey. `rell-portrait.png` is a second generation of the same picture without the lettering the first one had on its shoulder patch.
