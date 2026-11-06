// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

/*! \brief Data description of a procedurally generated block/item texture. */
struct QcTextureSpec
{
    /*! What the texture shows; the colours, the seed and the bitmap fill it in. */
    enum class Pattern {
        Noise, // the primary colour with pixel variation
        Speckle, // like Noise with scattered pixels in the secondary colour
        Planks, // staggered boards with seams
        Bricks, // bricks (primary) in mortar
        LogSide, // bark of a log
        LogTop, // a log's cut with growth rings
        Leaves, // leaves with gaps
        Glass, // a pane with a frame; keeps transparency
        GrassSide, // soil with a grass rim on top
        Ore, // stone (primary) with clusters of ore (secondary)
        Water, // water with waves, animated
        Tool, // a plain tool on the diagonal: handle and head
        Lump, // a round lump (primary, spots secondary) on a transparent background
        Pickaxe, // tool icon: a pickaxe
        Axe, // tool icon: an axe
        Shovel, // tool icon: a shovel
        Sword, // tool icon: a sword
        Stick, // tool icon: a stick without head
        Ingot, // item icon: an ingot
        Gem, // item icon: a cut gem
        /*! Small plant: a stem in the secondary colour under a leafy crown in the primary
            colour. */
        Sapling,
        /*! Eyes (primary with secondary pupils) and a mouth on a transparent background, for a
            thin box in front of a head. */
        Face,
        /*! Machine front: a stone face (primary) with a dark fire opening (secondary) and a
            grate. */
        Furnace,
        /*! Torch stretched over a thin box: a flame (secondary) on top of a stick (primary). */
        Torch,
        /*! Torch as a flat icon on a transparent background. */
        TorchItem,
        /*! Two rails with rungs (primary) on a transparent background. */
        Ladder,
        /*! Door halves: vertical boards (primary) in a frame (secondary); the lower one with a
            handle, the upper one with a window. */
        DoorLower,
        DoorUpper, // the upper half, with the window
        /*! The whole door as an icon: both halves in a narrow frame, the window above, the
            handle below, on a transparent background (decision E-831). */
        DoorItem,
        /*! Hatch of boards with a frame and two openings. */
        Trapdoor,
        /*! Chest: boards (primary) with darker edges and a lid seam; the front adds a latch
            (secondary), the top only has the frame. */
        ChestSide,
        ChestFront, // the front with the latch
        ChestTop, // the top with the frame
        /*! Molten rock: glowing streaks (secondary) in a darker flow (primary). */
        Lava,
        /*! Flames on a transparent background (decision E-392): tongues rising from the bottom,
            the secondary colour in their core, the primary at their edges. */
        Fire,
        /*! Blossom (primary) with a light centre on a stem with two leaves (secondary). */
        Flower,
        /*! Blades of grass of different heights (primary, tips secondary). */
        TallGrass,
        /*! Cap (primary, lighter spots) on a pale stem. */
        Mushroom,
        /*! Three jointed stalks (primary) with a few leaves (secondary). */
        SugarCane,
        /*! Glass bottle with a cork; the contents in the secondary colour (transparent: empty). */
        Bottle,
        /*! Metal bucket (primary); the contents seen from above in the secondary colour. */
        Bucket,
        /*! Lamp housing (primary) around a glowing window (secondary) with bars, for a stretched
            box. */
        Lantern,
        /*! Bare skin of an arm or hand (meant to be tinted): rounded shading towards the edges,
            soft mottling, at the bottom a knuckle line and fingers (secondary). */
        Limb,
        /*! Sleeve fabric (meant to be tinted): fine weave, rounded shading and folds, a darker
            cuff with a stitched seam at the bottom. */
        Sleeve,
        /*! Patterns of QcTextureCraftPatterns (storage blocks, crafted items, small plants).
            A few small grains (primary, highlights secondary) on a transparent background. */
        Nugget,
        /*! Storage block: a solid face (primary) with a bevelled frame and highlights
            (secondary). */
        MetalBlock,
        /*! Vertical bars (primary) with two cross bars on a transparent background. */
        Bars,
        /*! Bookshelf side: planks (primary) holding two rows of coloured book spines. */
        Bookshelf,
        /*! Sheet of paper (primary) with faint lines (secondary). */
        Paper,
        /*! Book: cover (primary) with pages (secondary) at the edge. */
        Book,
        /*! Wooden bowl (primary) seen from the side; Stew fills it with soup (secondary). */
        Bowl,
        Stew, // the bowl filled with soup
        /*! Round fruit (primary) with a stalk and leaf (secondary). */
        Apple,
        /*! Hide (primary) with stitches (secondary); Scrap is a ragged small piece of it. */
        Leather,
        Scrap, // a ragged small piece of hide
        /*! Coiled thread (primary). */
        String,
        /*! Web of threads (primary) on a transparent background. */
        Cobweb,
        /*! Dry twigs (primary) without leaves. */
        DeadBush,
        /*! Fern fronds (primary, tips secondary). */
        Fern,
        /*! Glass (primary) with glowing dots (secondary); keeps partial transparency. */
        FireflyGlass,
        /*! Barrel: vertical staves (primary) with metal hoops (secondary); the top shows a lid. */
        BarrelSide,
        BarrelTop, // the top with the lid
        /*! Tinted glass: a frame (secondary) around a clearly coloured pane (primary); keeps
            partial transparency. */
        StainedGlass,
        /*! Raw metal (decision E-740): a lumpy chunk (primary, stony spots and rim secondary) on
            a transparent background, and the block of raw metal: rounded lumps with crevices
            (secondary) between them. */
        RawChunk,
        RawBlock, // the block of raw metal
        /*! Chain links (primary, highlights secondary) on a transparent background, running from
            top to bottom through the middle four columns; ChainAcross runs from left to right
            through the middle four rows (decision E-742). */
        Chain,
        ChainAcross, // links running from left to right
        /*! The inside of a portal (decision E-1022): swirling bands of light (secondary) in a
            translucent glow (primary) that turn around the middle once per loop. */
        Portal,
        /*! A leather backpack (decision E-832): sides with a strap and a buckle (secondary), the
            front with a stitched flap and its buckle, the top with a carrying loop; all with a
            darker seam at the edges. BackpackItem is its icon: the body with a loop, a lighter
            flap with its buckle and a pocket, on a transparent background. */
        BackpackSide,
        BackpackFront, // the front with the flap
        BackpackTop, // the top with the loop
        BackpackItem, // the icon
        /*! Patterns of QcTextureFarmPatterns (decision E-289): a hoe (tool head like the others)
            ... */
        Hoe,
        /*! ... crop plants on a transparent background: stalks (primary) with ears (secondary),
            leafy tufts (primary) showing roots or tubers (secondary) at the bottom, a thin stem
            with leaves ... */
        Crop,
        RootCrop, // leafy tufts with roots or tubers
        Stem, // a thin stem with leaves
        NetherWart, // knobbly lumps with gaps (rims dark, lit sides secondary)
        /*! ... tilled soil with furrows (primary, furrows secondary) ... */
        Farmland,
        /*! ... fruit blocks: a ribbed pumpkin side and its top with the stalk, a striped melon
            ... */
        PumpkinSide,
        PumpkinTop, // the pumpkin's top with the stalk
        MelonSide, // the striped melon
        /*! ... and food icons: a sheaf of wheat, a loaf, a carrot, a melon slice, a pie. */
        Wheat,
        Bread, // a loaf
        Carrot, // a carrot
        MelonSlice, // a melon slice
        Pie, // a pie
        /*! ... the hay bale: straw running from top to bottom (primary, darker stalks secondary)
            tied by two bands, its top with cut stalk ends; and flint and steel: a steel striker
            (secondary) bent around a dark flint chip (primary), an item icon (decision E-393). */
        HaySide,
        HayTop, // the hay bale's top
        FlintAndSteel, // flint and steel
        CarvedPumpkin, // a pumpkin face
        Shears, // shears
        /*! Patterns of QcTextureArmourPatterns (decision E-235): item icons in the material
            (primary) with a light upper edge (secondary) ... */
        Helmet,
        Chestplate, // chestplate icon
        Leggings, // leggings icon
        Boots, // boots icon
        /*! ... and what armour looks like on a body: a riveted plate, stitched leather, a helmet
            front with an opening for the face, a visor with eye slits, polished trim with a
            ridge. */
        ArmourPlate,
        ArmourLeather, // stitched leather
        HelmetFront, // helmet front with an opening
        Visor, // visor with eye slits
        ArmourTrim, // polished trim
        /*! Icon of an armour stand: a post with bars (primary) on a stone plate (secondary). */
        ArmourStand,
        /*! Pixel art from \c {bitmap} (QcTextureEmblem): each character picks a colour of
            \c {palette}, any other character (and every pixel the bitmap leaves out) shows woven
            cloth in the primary colour. For cape emblems and hems (decision E-241). */
        Emblem,
        /*! Patterns of QcTextureSignalPatterns (decision E-333): loose grains of the wire
            (primary, some secondary) on a transparent background, a lever icon (stick primary,
            base secondary), a repeater's slab (primary) with a wire and arrow (secondary), a
            piston's stone side (primary) in a wooden frame (secondary). */
        Dust,
        Lever, // lever icon
        Repeater, // repeater slab
        PistonSide, // piston side
        /*! Signal machines (decision E-342): a hopper's riveted iron side (primary, rivets and
            seams secondary), its rim seen from above around a dark bowl (secondary), its item
            icon (a funnel), a comparator's slab (primary) with three torch spots and a wire
            (secondary), an observer's face (primary) with a slit of eyes (secondary), its back
            with the output spot (secondary), its side with an arrow, and the fronts of dispenser
            and dropper: a stone face (primary) with a round mouth or a narrow slot (secondary).
            Rail: two iron rails (primary) on wooden sleepers (secondary), as an item icon;
            minecart: an open iron tub (primary) with a dark inside and wheels (secondary), as an
            item icon (decision E-382). */
        Hopper,
        HopperTop, // hopper rim from above
        HopperItem, // hopper icon
        Comparator, // comparator slab
        DaylightSensor, // panes of a daylight sensor
        ObserverFace, // observer face
        ObserverBack, // observer back
        ObserverSide, // observer side
        ObserverTop, // observer top
        ObserverBottom, // observer bottom
        Dispenser, // dispenser front
        Dropper, // dropper front
        Rail, // rail icon
        Minecart, // minecart icon
        /*! Patterns of QcTextureRangedPatterns (decision E-425): a bow (wood primary, string
            secondary), an arrow (shaft primary, fletching secondary), a feather (vanes primary,
            quill secondary) and a bone (primary, its knobs shaded by secondary; decision E-426),
            all icons on the diagonal. */
        Bow,
        /*! The bow drawn with an arrow on it, in its three stages (decision E-987): limbs and
            string as Bow, pulled back further from stage to stage. */
        BowDrawn1,
        BowDrawn2, // the second stage
        BowDrawn3, // the third stage, fully drawn
        Arrow, // an arrow
        Feather, // a feather
        Bone, // a bone
        /*! A fishing rod (decision E-503): the rod (primary) on the diagonal, its line
            (secondary) hanging from the tip with a hook. */
        FishingRod,
        /*! Patterns of QcTextureBuildingPatterns (decision E-750): bricks (primary) with moss
            (secondary) in patches and in the mortar, bricks with mortar (secondary) and two
            cracks, a chiseled face of nested frames, a smooth face in a thin frame (secondary)
            and a glazed ornament (secondary on primary) without symmetry, so turned blocks show
            their facing. */
        MossyBricks,
        CrackedBricks, // cracked bricks
        ChiseledBricks, // a chiseled face
        SmoothStone, // a smooth face
        GlazedTerracotta, // a glazed ornament
        /*! Pixel art like Emblem, but every pixel the palette does not colour is transparent:
            item icons from data (signs, compass and clock, decisions E-766, E-769). */
        Icon,
        /*! Patterns of QcTextureKitchenPatterns (decisions E-772 to E-774): the cake's frosted
            top (primary) with berries, its side (frosting primary over sponge secondary, the
            upper half clear) and its icon, the campfire's icon (logs primary, their ends
            secondary, a flame) and a round lily pad with a notch and veins (secondary). */
        CakeTop,
        CakeSide, // the cake's side
        CakeItem, // the cake's icon
        CampfireItem, // the campfire's icon
        LilyPad, // a lily pad
        /*! Patterns of QcTexturePaintingPatterns (decision E-791): an item frame (a wooden
            border in the primary colour around leather in the secondary) and painting motifs
            drawn over a canvas of tilesWide x tilesHigh blocks: a landscape of hills under a
            sun, the sea with a sail, an abstract of coloured fields between dark lines, a night
            with stars over mountains, a sunset over water and rings spreading over a ground;
            primary and secondary colour and the seed vary each. */
        ItemFrame,
        PaintingLandscape, // hills under a sun
        PaintingSea, // the sea with a sail
        PaintingAbstract, // coloured fields between dark lines
        PaintingNight, // stars over mountains
        PaintingSunset, // a sunset over water
        PaintingRings, // rings spreading over a ground
        /*! Ice without a frame, tiling without seams (QcTextureBuildingPatterns): a smooth face
            (primary, its alpha kept, so ice stays translucent and blue ice opaque) with soft
            diagonal streaks (secondary); packed ice is opaque with fine wandering cracks
            (secondary). */
        Ice,
        PackedIce, // packed ice
        /*! Slime (decision E-860): a translucent fill in the primary colour (its alpha kept)
            with a one-pixel rim in the secondary colour (its alpha kept too); the slime block
            draws it on its outer cube and, darker and more opaque, on its inner core cube. */
        Slime,
        /*! Reeds (decision E-850): thin stalks with long leaves (primary), a few with a brown
            head (secondary). */
        Reeds,
        /*! A creature's coat (QcTextureCoatPatterns, decision E-880): a few large smooth-edged
            patches (secondary) on the primary colour, like a Holstein cow; the seed places them. */
        Patches,
        /*! The front of a skull (QcTextureCoatPatterns): bone (primary) with two eye sockets and a
            nose hole (secondary), a faint glow deep in the sockets, and a row of teeth. */
        Skull,
        /*! The relief patterns (QcTextureRelief, decision E-1043), painted at each resolution.
            Porous fused rock: crooked lumps (primary) with seams and pores (secondary). */
        FusedRock,
        /*! Grainy sand (primary) with a few deep hollows (secondary). */
        HollowSand,
        /*! Dry soil: flat angular plates (primary) between cracks with pale grit (secondary). */
        CrustedSoil,
        /*! Standing stone columns (primary) with joints and fractures (secondary). */
        ColumnSide,
        /*! The cut through such columns: polygons (primary) with joints (secondary). */
        ColumnTop,
        /*! The columns ground flat: a smooth face (primary) with fine joints (secondary). */
        PolishedColumns,
        /*! Ground rock with wavering flow bands (primary, secondary). */
        FlowBands,
        /*! Dark stone (primary) breaking like glass: shell-shaped shards with glints (secondary). */
        GlassyStone,
        /*! That stone polished: dark clouded fields (primary to secondary) with pale veins. */
        PolishedGlass,
        /*! Glassy stone (primary) with gold (secondary) in its fractures. */
        GildedStone,
        /*! Large smooth blocks (primary) in courses of unequal height with thin joints
            (secondary); cracked, and carved with a stepped diamond. */
        Ashlar,
        CrackedAshlar, // with cracks
        CarvedAshlar, // with a carved panel
        /*! Long fired bricks of unequal length (primary) in deep mortar (secondary), with scorch
            marks; cracked, and carved with chevrons. */
        FiredBricks,
        CrackedFiredBricks, // with cracks
        CarvedFiredBricks, // with a carved panel
        /*! Fused rock (primary) with crystals standing out of it (secondary). */
        QuartzOre,
        /*! Fused rock (primary) with crooked strands and flakes of gold (secondary). */
        GoldVeinOre,
        /*! A dark crust (primary) broken into plates of different sizes over thin cracks of molten
            rock (secondary). */
        MagmaCrust,
        /*! Glowing lumps (primary, white in their middle) with dim gaps between them (secondary). */
        GlowCluster,
        /*! The nether forests (decision E-1048). A mat of crossing threads (primary, secondary in
            the furrows) with fleshy knobs. */
        FungalFelt,
        /*! Round cushions of fine tufts (primary) with crevices (secondary). */
        MossCushions,
        /*! A mat (primary) hanging over rock (secondary) in uneven tongues; does not repeat
            downwards, like the side of a grass block. */
        MatEdge,
        /*! Twisted ropes of fibre (primary) with furrows (secondary). */
        RopeStem,
        /*! Dark upright ribs (primary) with shining pores (secondary). */
        PoredStem,
        /*! The cut through a stem: tube walls (primary) around hollows (secondary). */
        StemSection,
        /*! Bare fibre (primary) with a few dark flecks (secondary). */
        PeeledFibre,
        /*! Long boards across the tile (primary) with seams and butt joints (secondary). */
        FibreBoards,
        /*! Upright boards of unequal width (primary), their fibre full of pits (secondary). */
        PittedBoards,
        /*! Flesh in lumps with small warts (primary), dark gaps (secondary). */
        WartLumps,
        /*! Flesh in winding folds (primary) with dark grooves (secondary). */
        WartFolds,
        /*! Glowing lobes (primary, pale in their middle) in a thin skin (secondary). */
        GlowLobes,
    };

    /*! Emblem and Icon only: up to 16 rows of up to 16 characters, top row first. \qcshared */
    static constexpr int maxBitmapRows = 16;
    static constexpr int maxPaletteEntries = 16;

    /*! The pattern. */
    Pattern pattern = Pattern::Noise;
    /*! The main colour. */
    QColor primary = QColor(128, 128, 128);
    /*! The second colour (seams, spots, contents). */
    QColor secondary = QColor(96, 96, 96);
    /*! How strongly the pixels vary around their colour, 0..1. */
    double variation = 0.15;
    /*! Varies the pixels and the placement of details; the same seed gives the same texture. */
    quint32 seed = 0;
    /*! Emblem and Icon: the rows of the picture, one character per pixel (QcTextureEmblem). The
        initialisers of this field and the next keep aggregate initialisations that leave them
        out free of -Wmissing-field-initializers. */
    QStringList bitmap = {};       // NOLINT(readability-redundant-member-init): see above
    /*! Emblem and Icon: the colour of each character of the bitmap. */
    QHash<QChar, QColor> palette = {}; // NOLINT(readability-redundant-member-init): see above
    /*! A texture over several blocks (decision E-791, "tiles"): its size in blocks and, for one
        of its tiles ("tile"), the column and row (from the top) this texture shows; without a
        tile the whole canvas is drawn shrunk into one block (an icon). \qcshared */
    int tilesWide = 1;
    int tilesHigh = 1;
    std::optional<std::pair<int, int>> tile = std::nullopt;
    /*! Where in its loop an animation frame lies (decision E-878): frame / frames, in [0, 1).
        The patterns that animate (fire, lava, water) move with it so the last frame leads
        seamlessly into the first; 0 paints the still texture. */
    double phase = 0.0;
    /*! Whether the picture is the base of a texture enlarged above 16 x 16 (QcTextureDetail,
        decision E-1000): a pattern may then add parts the 16 x 16 icon leaves out because they
        would be lost or crowded at one pixel, such as a sword's guard. The 16 x 16 picture stays
        unchanged. */
    bool detailed = false;
};

/*! Returns the pattern named \a name in the content ("planks", "log_side", ...); nothing for an
    unknown name. */
[[nodiscard]] std::optional<QcTextureSpec::Pattern> qcTexturePatternFromName(const QString &name);
