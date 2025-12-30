# 3D Math Reminders
These exist to refresh intuition, not teach math.

## View Matrix
A **view matrix** answers one simple question:
- *"If the camera were at origin* `(0,0,0)` *looking straight ahead, where would the world appear?"*

Instead of moving the camera we:
- **Move and rotate the entire world** so that
- the camera *acts like* it's sitting at `(0,0,0)` looking down `-Z`.

## Mental model: the camera's local axes
A camera needs **three directions** to describe how it's oriented:
- **Forward**: -> where the camera looks
- **Right**: -> where "right" is from the camera's perspective
- **Up**: -> which way is "up" for the camera

These three directions form a **coordinate system**.

Once we know those three directions, we can:
1. **Rotate the world** so it matches the camera's orientation
2. **Translate the world** so the camera ends up at the origin