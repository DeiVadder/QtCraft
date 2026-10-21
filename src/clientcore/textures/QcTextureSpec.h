#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

// Data description of a procedurally generated block/item texture.
struct QcTextureSpec
{
    enum class Pattern {
        Noise,
        Speckle,
        Planks,
        Bricks,
        LogSide,
        LogTop,
        Leaves,
        Glass,
        GrassSide,
        Ore,
        Water,
        Tool,
        Lump,
        Pickaxe,
        Axe,
        Shovel,
        Sword,
        Stick,
        Ingot,
        Gem,
        // Small plant: a stem in the secondary colour under a leafy crown in the primary colour.
        Sapling,
        // Eyes (primary with secondary pupils) and a mouth on a transparent background, for a thin
        // box in front of a head.
        Face,
        // Machine front: a stone face (primary) with a dark fire opening (secondary) and a grate.
        Furnace,
        // Torch stretched over a thin box: a flame (secondary) on top of a stick (primary).
        Torch,
        // Torch as a flat icon on a transparent background.
        TorchItem,
        // Two rails with rungs (primary) on a transparent background.
        Ladder,
        // Door halves: vertical boards (primary) in a frame (secondary); the lower one with a
        // handle, the upper one with a window.
        DoorLower,
        DoorUpper,
        // The whole door as an icon: both halves in a narrow frame, the window above, the handle
        // below, on a transparent background (decision E-831).
        DoorItem,
        // Hatch of boards with a frame and two openings.
        Trapdoor,
        // Chest: boards (primary) with darker edges and a lid seam; the front adds a latch
        // (secondary), the top only has the frame.
        ChestSide,
        ChestFront,
        ChestTop,
        // Molten rock: glowing streaks (secondary) in a darker flow (primary).
        Lava,
        // Flames on a transparent background (decision E-392): tongues rising from the bottom, the
        // secondary colour in their core, the primary at their edges.
        Fire,
        // Blossom (primary) with a light centre on a stem with two leaves (secondary).
        Flower,
        // Blades of grass of different heights (primary, tips secondary).
        TallGrass,
        // Cap (primary, lighter spots) on a pale stem.
        Mushroom,
        // Three jointed stalks (primary) with a few leaves (secondary).
        SugarCane,
        // Glass bottle with a cork; the contents in the secondary colour (transparent: empty).
        Bottle,
        // Metal bucket (primary); the contents seen from above in the secondary colour.
        Bucket,
        // Lamp housing (primary) around a glowing window (secondary) with bars, for a stretched box.
        Lantern,
        // Bare skin of an arm or hand (meant to be tinted): rounded shading towards the edges, soft
        // mottling, at the bottom a knuckle line and fingers (secondary).
        Limb,
        // Sleeve fabric (meant to be tinted): fine weave, rounded shading and folds, a darker cuff
        // with a stitched seam at the bottom.
        Sleeve,
        // Patterns of QcTextureCraftPatterns (storage blocks, crafted items, small plants).
        // A few small grains (primary, highlights secondary) on a transparent background.
        Nugget,
        // Storage block: a solid face (primary) with a bevelled frame and highlights (secondary).
        MetalBlock,
        // Vertical bars (primary) with two cross bars on a transparent background.
        Bars,
        // Bookshelf side: planks (primary) holding two rows of coloured book spines.
        Bookshelf,
        // Sheet of paper (primary) with faint lines (secondary).
        Paper,
        // Book: cover (primary) with pages (secondary) at the edge.
        Book,
        // Wooden bowl (primary) seen from the side; Stew fills it with soup (secondary).
        Bowl,
        Stew,
        // Round fruit (primary) with a stalk and leaf (secondary).
        Apple,
        // Hide (primary) with stitches (secondary); Scrap is a ragged small piece of it.
        Leather,
        Scrap,
        // Coiled thread (primary).
        String,
        // Web of threads (primary) on a transparent background.
        Cobweb,
        // Dry twigs (primary) without leaves.
        DeadBush,
        // Fern fronds (primary, tips secondary).
        Fern,
        // Glass (primary) with glowing dots (secondary); keeps partial transparency.
        FireflyGlass,
        // Barrel: vertical staves (primary) with metal hoops (secondary); the top shows a lid.
        BarrelSide,
        BarrelTop,
        // Tinted glass: a frame (secondary) around a clearly coloured pane (primary); keeps partial
        // transparency.
        StainedGlass,
        // Raw metal (decision E-740): a lumpy chunk (primary, stony spots and rim secondary) on a
        // transparent background, and the block of raw metal: rounded lumps with crevices
        // (secondary) between them.
        RawChunk,
        RawBlock,
        // Chain links (primary, highlights secondary) on a transparent background, running from top
        // to bottom through the middle four columns; ChainAcross runs from left to right through the
        // middle four rows (decision E-742).
        Chain,
        ChainAcross,
        // A leather backpack (decision E-832): sides with a strap and a buckle (secondary), the
        // front with a stitched flap and its buckle, the top with a carrying loop; all with a
        // darker seam at the edges. BackpackItem is its icon: the body with a loop, a lighter flap
        // with its buckle and a pocket, on a transparent background.
        BackpackSide,
        BackpackFront,
        BackpackTop,
        BackpackItem,
        // Patterns of QcTextureFarmPatterns (decision E-289): a hoe (tool head like the others) ...
        Hoe,
        // ... crop plants on a transparent background: stalks (primary) with ears (secondary),
        // leafy tufts (primary) showing roots or tubers (secondary) at the bottom, a thin stem with
        // leaves ...
        Crop,
        RootCrop,
        Stem,
        // ... tilled soil with furrows (primary, furrows secondary) ...
        Farmland,
        // ... fruit blocks: a ribbed pumpkin side and its top with the stalk, a striped melon ...
        PumpkinSide,
        PumpkinTop,
        MelonSide,
        // ... and food icons: a sheaf of wheat, a loaf, a carrot, a melon slice, a pie.
        Wheat,
        Bread,
        Carrot,
        MelonSlice,
        Pie,
        // ... the hay bale: straw running from top to bottom (primary, darker stalks secondary)
        // tied by two bands, its top with cut stalk ends; and flint and steel: a steel striker
        // (secondary) bent around a dark flint chip (primary), an item icon (decision E-393).
        HaySide,
        HayTop,
        FlintAndSteel,
        CarvedPumpkin,
        Shears,
        // Patterns of QcTextureArmourPatterns (decision E-235): item icons in the material
        // (primary) with a light upper edge (secondary) ...
        Helmet,
        Chestplate,
        Leggings,
        Boots,
        // ... and what armour looks like on a body: a riveted plate, stitched leather, a helmet
        // front with an opening for the face, a visor with eye slits, polished trim with a ridge.
        ArmourPlate,
        ArmourLeather,
        HelmetFront,
        Visor,
        ArmourTrim,
        // Icon of an armour stand: a post with bars (primary) on a stone plate (secondary).
        ArmourStand,
        // Pixel art from `bitmap` (QcTextureEmblem): each character picks a colour of `palette`,
        // any other character (and every pixel the bitmap leaves out) shows woven cloth in the
        // primary colour. For cape emblems and hems (decision E-241).
        Emblem,
        // Patterns of QcTextureSignalPatterns (decision E-333): loose grains of the wire (primary,
        // some secondary) on a transparent background, a lever icon (stick primary, base
        // secondary), a repeater's slab (primary) with a wire and arrow (secondary), a piston's
        // stone side (primary) in a wooden frame (secondary).
        Dust,
        Lever,
        Repeater,
        PistonSide,
        // Signal machines (decision E-342): a hopper's riveted iron side (primary, rivets and seams
        // secondary), its rim seen from above around a dark bowl (secondary), its item icon (a funnel),
        // a comparator's slab (primary) with three torch spots and a wire (secondary), an observer's
        // face (primary) with a slit of eyes (secondary), its back with the output spot (secondary),
        // its side with an arrow, and the fronts of dispenser and dropper: a stone face (primary)
        // with a round mouth or a narrow slot (secondary). Rail: two iron rails (primary) on wooden
        // sleepers (secondary), as an item icon; minecart: an open iron tub (primary) with a dark inside
        // and wheels (secondary), as an item icon (decision E-382).
        Hopper,
        HopperTop,
        HopperItem,
        Comparator,
        ObserverFace,
        ObserverBack,
        ObserverSide,
        Dispenser,
        Dropper,
        Rail,
        Minecart,
        // Patterns of QcTextureRangedPatterns (decision E-425): a bow (wood primary, string
        // secondary), an arrow (shaft primary, fletching secondary), a feather (vanes primary,
        // quill secondary) and a bone (primary, its knobs shaded by secondary; decision E-426), all
        // icons on the diagonal.
        Bow,
        Arrow,
        Feather,
        Bone,
        // A fishing rod (decision E-503): the rod (primary) on the diagonal, its line (secondary)
        // hanging from the tip with a hook.
        FishingRod,
        // Patterns of QcTextureBuildingPatterns (decision E-750): bricks (primary) with moss
        // (secondary) in patches and in the mortar, bricks with mortar (secondary) and two cracks,
        // a chiseled face of nested frames, a smooth face in a thin frame (secondary) and a glazed
        // ornament (secondary on primary) without symmetry, so turned blocks show their facing.
        MossyBricks,
        CrackedBricks,
        ChiseledBricks,
        SmoothStone,
        GlazedTerracotta,
        // Pixel art like Emblem, but every pixel the palette does not colour is transparent: item
        // icons from data (signs, compass and clock, decisions E-766, E-769).
        Icon,
        // Patterns of QcTextureKitchenPatterns (decisions E-772 to E-774): the cake's frosted top
        // (primary) with berries, its side (frosting primary over sponge secondary, the upper half
        // clear) and its icon, the campfire's icon (logs primary, their ends secondary, a flame) and
        // a round lily pad with a notch and veins (secondary).
        CakeTop,
        CakeSide,
        CakeItem,
        CampfireItem,
        LilyPad,
        // Patterns of QcTexturePaintingPatterns (decision E-791): an item frame (a wooden border in
        // the primary colour around leather in the secondary) and painting motifs drawn over a
        // canvas of tilesWide x tilesHigh blocks: a landscape of hills under a sun, the sea with a
        // sail, an abstract of coloured fields between dark lines, a night with stars over
        // mountains, a sunset over water and rings spreading over a ground; primary and secondary
        // colour and the seed vary each.
        ItemFrame,
        PaintingLandscape,
        PaintingSea,
        PaintingAbstract,
        PaintingNight,
        PaintingSunset,
        PaintingRings,
    };

    // Emblem and Icon only: up to 16 rows of up to 16 characters, top row first.
    static constexpr int maxBitmapRows = 16;
    static constexpr int maxPaletteEntries = 16;

    Pattern pattern = Pattern::Noise;
    QColor primary = QColor(128, 128, 128);
    QColor secondary = QColor(96, 96, 96);
    double variation = 0.15;
    quint32 seed = 0;
    // The initialisers keep aggregate initialisations that leave these out free of
    // -Wmissing-field-initializers.
    QStringList bitmap = {};       // NOLINT(readability-redundant-member-init): see above
    QHash<QChar, QColor> palette = {}; // NOLINT(readability-redundant-member-init): see above
    // A texture over several blocks (decision E-791, "tiles"): its size in blocks and, for one of
    // its tiles ("tile"), the column and row (from the top) this texture shows; without a tile the
    // whole canvas is drawn shrunk into one block (an icon).
    int tilesWide = 1;
    int tilesHigh = 1;
    std::optional<std::pair<int, int>> tile = std::nullopt;
};

[[nodiscard]] std::optional<QcTextureSpec::Pattern> qcTexturePatternFromName(const QString &name);
