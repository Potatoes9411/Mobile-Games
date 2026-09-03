/* ===========================================================================
   ARCADE ENGINE - pseudo 3D camera
   A pinhole projector for the lane based games. Anything at a constant depth
   projects to a screen space rectangle, which is why gates, walls and tower
   rooms can be drawn and hit tested without any real 3D.
   =========================================================================== */
(function (A) {
  "use strict";

  A.Camera3D = function () {
    var cam = {
      x: 0, y: 9.6, z: -13.5,
      focal: 500,
      horizon: 0,

      fit: function (w, h, horizonRatio, focalRatio) {
        cam.focal = Math.min(h * (focalRatio || 0.56), w * 0.95);
        cam.horizon = h * (horizonRatio || 0.30);
      },

      moveTo: function (x, y, z, rate, dt) {
        if (rate === undefined) { cam.x = x; cam.y = y; cam.z = z; return; }
        var k = 1 - Math.exp(-rate * dt);
        cam.x += (x - cam.x) * k;
        cam.y += (y - cam.y) * k;
        cam.z += (z - cam.z) * k;
      },

      project: function (x, y, z) {
        var dz = z - cam.z;
        if (dz <= 1.4) return null;
        var s = cam.focal / dz;
        return { x: A.View.w / 2 + (x - cam.x) * s, y: cam.horizon + (cam.y - y) * s, s: s };
      },

      /** Screen frame for the plane at depth z: px(worldX), py(worldY), s. */
      plane: function (z) {
        var dz = z - cam.z;
        if (dz <= 1.4) return null;
        var s = cam.focal / dz;
        var ox = A.View.w / 2 - cam.x * s;
        var oy = cam.horizon + cam.y * s;
        return {
          s: s,
          px: function (wx) { return ox + wx * s; },
          py: function (wy) { return oy - wy * s; }
        };
      },

      fog: function (z, near, far) {
        return A.clamp01((z - cam.z - (near || 34)) / ((far || 165) - (near || 34)));
      }
    };

    return cam;
  };

  /** Fills a quad from four screen points. */
  A.quad = function (g, a, b, c, d, fill, stroke, lineWidth) {
    g.beginPath();
    g.moveTo(a[0], a[1]);
    g.lineTo(b[0], b[1]);
    g.lineTo(c[0], c[1]);
    g.lineTo(d[0], d[1]);
    g.closePath();
    if (fill) { g.fillStyle = fill; g.fill(); }
    if (stroke) { g.strokeStyle = stroke; g.lineWidth = lineWidth || 2; g.stroke(); }
  };

  /** Ground strip between two depths, in world x/y. */
  A.strip = function (g, cam, x0, x1, y0, y1, zNear, zFar, fill) {
    var n = cam.plane(zNear), f = cam.plane(zFar);
    if (!n || !f) return;
    A.quad(g,
      [n.px(x0), n.py(y0)], [n.px(x1), n.py(y1)],
      [f.px(x1), f.py(y1)], [f.px(x0), f.py(y0)], fill);
  };
})(window.A);
