#pragma once

#include "QcEntityPose.h"
#include "QcVec3.h"

// How a figure moves, as far as its cape cares: speeds in m/s in the figure's own frame.
struct QcCapeDrive
{
    // Along the facing direction (negative when walking backwards).
    double forwardSpeed = 0.0;
    // To the figure's right.
    double sideSpeed = 0.0;
    // Upwards (negative when falling).
    double verticalSpeed = 0.0;
    bool onGround = true;
    bool inWater = false;
    bool sneaking = false;

    // From a velocity in world space and the figure's yaw (degrees, 0 faces -Z, 90 faces +X).
    static QcCapeDrive from(const QcVec3 &velocity, double yaw, const QcEntityPose &pose);
};

// The swing of a cape (decision E-240): a damped spring per axis pulls the cape towards an angle
// that follows from the movement: lifted backwards by walking and more by sprinting, upwards when
// falling, pressed to the back when rising or sneaking, held off the swinging legs when walking
// backwards, swaying gently when standing. Integrated in fixed steps, so the result does not
// depend on the frame rate; GUI-free.
class QcCapeSwing
{
public:
    // Degrees against the back as in QcAnimator::BodyPose::capeLift/capeSway.
    struct Angles
    {
        double lift = 0.0;
        double sway = 0.0;
    };

    static constexpr double minLift = 0.0;
    static constexpr double maxLift = 150.0;
    static constexpr double maxSway = 25.0;
    // Lift at rest: the cape stands off the back a little.
    static constexpr double restLift = 5.0;
    // Integration step and the longest time one call integrates (a longer pause is cut short).
    static constexpr double step = 1.0 / 120.0;
    static constexpr double maxAdvance = 0.25;

    // Where the spring pulls the cape for `drive` at `time` (seconds of a free-running clock,
    // driving the idle sway and the flutter).
    static Angles target(const QcCapeDrive &drive, double time);

    // Moves the cape `seconds` further; `time` is the clock at the end of that span.
    void advance(const QcCapeDrive &drive, double seconds, double time);
    // Puts the cape at rest on its target (first sight, fixed screenshots).
    void settle(const QcCapeDrive &drive, double time);
    Angles angles() const { return {m_lift.angle, m_sway.angle}; }

private:
    struct Axis
    {
        double angle = 0.0;
        double velocity = 0.0;

        void step(double target, double low, double high);
    };

    Axis m_lift{restLift, 0.0};
    Axis m_sway;
    // Time not integrated yet (less than one step).
    double m_pending = 0.0;
};
