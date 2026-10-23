using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

namespace ThinPlaceVR
{
    // Traces the outline of a steering wheel rim from a front-on photo.
    //
    // Works best with a product cut-out (transparent background) or a photo on a plain background.
    // Steps: separate the rim from the background, keep the biggest shape, fill its holes (grips),
    // follow its outer edge, then simplify the edge to a few hundred points.
    internal sealed class TraceResult
    {
        public int Width, Height;               // working image size (px)
        public List<PointF> Outline;            // outer edge in image px
        public RectangleF Bounds;               // of the outline
        public string ThumbnailDataUrl;         // the photo, for showing the outline over it
    }

    internal static class Tracer
    {
#if !TRACER_TEST
        public static TraceResult Trace(string file)
        {
            using (var img = LoadScaled(file, 900))
            {
                int w = img.Width, h = img.Height;
                int[] argb = ReadPixels(img);
                var outline = OutlineFromMask(Foreground(argb, w, h), w, h);

                float minX = float.MaxValue, minY = float.MaxValue, maxX = float.MinValue, maxY = float.MinValue;
                foreach (var p in outline)
                {
                    minX = Math.Min(minX, p.X); maxX = Math.Max(maxX, p.X);
                    minY = Math.Min(minY, p.Y); maxY = Math.Max(maxY, p.Y);
                }
                if (maxX - minX < 40 || maxY - minY < 20) throw new Exception("The shape found is too small. Try a larger, front-on photo of the rim.");

                return new TraceResult
                {
                    Width = w,
                    Height = h,
                    Outline = outline,
                    Bounds = RectangleF.FromLTRB(minX, minY, maxX, maxY),
                    ThumbnailDataUrl = Thumbnail(img),
                };
            }
        }
#endif

        // Everything after separating the rim from the background (also used by the tests).
        internal static List<PointF> OutlineFromMask(bool[] mask, int w, int h)
        {
            mask = Close(mask, w, h, 2);
            mask = LargestShape(mask, w, h);
            mask = FillHoles(mask, w, h);
            var edge = FollowEdge(mask, w, h);
            if (edge.Count < 20) throw new Exception("Couldn't find a clear rim shape in that photo. A cut-out or plain background works best.");
            return Simplify(edge, 1.5f);
        }

#if !TRACER_TEST
        // ---- loading -----------------------------------------------------------------------

        private static Bitmap LoadScaled(string file, int maxSide)
        {
            using (var src = Image.FromFile(file))
            {
                float s = Math.Min(1f, (float)maxSide / Math.Max(src.Width, src.Height));
                int w = Math.Max(1, (int)(src.Width * s)), h = Math.Max(1, (int)(src.Height * s));
                var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
                using (var g = Graphics.FromImage(bmp))
                {
                    g.Clear(Color.Transparent);
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.DrawImage(src, 0, 0, w, h);
                }
                return bmp;
            }
        }

        private static int[] ReadPixels(Bitmap bmp)
        {
            var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                var px = new int[bmp.Width * bmp.Height];
                for (int y = 0; y < bmp.Height; y++)
                    Marshal.Copy(data.Scan0 + y * data.Stride, px, y * bmp.Width, bmp.Width);
                return px;
            }
            finally { bmp.UnlockBits(data); }
        }

        private static string Thumbnail(Bitmap img)
        {
            using (var ms = new MemoryStream())
            {
                img.Save(ms, ImageFormat.Png);
                return "data:image/png;base64," + Convert.ToBase64String(ms.ToArray());
            }
        }

#endif

        // ---- separating rim from background -------------------------------------------------

        internal static bool[] Foreground(int[] px, int w, int h)
        {
            var mask = new bool[w * h];

            // A cut-out: use its transparency.
            int clear = 0;
            for (int i = 0; i < px.Length; i += 7) if (((px[i] >> 24) & 0xFF) < 200) clear++;
            if (clear > px.Length / 7 / 20)
            {
                for (int i = 0; i < px.Length; i++) mask[i] = ((px[i] >> 24) & 0xFF) > 40;
                return mask;
            }

            // Otherwise: the background is the colour around the edges of the photo.
            long r = 0, g = 0, b = 0, n = 0;
            void Add(int x, int y) { int c = px[y * w + x]; r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF; n++; }
            for (int x = 0; x < w; x++) { Add(x, 0); Add(x, h - 1); }
            for (int y = 0; y < h; y++) { Add(0, y); Add(w - 1, y); }
            int br = (int)(r / n), bg = (int)(g / n), bb = (int)(b / n);
            const int threshold = 48 * 48;
            for (int i = 0; i < px.Length; i++)
            {
                int c = px[i];
                int dr = ((c >> 16) & 0xFF) - br, dg = ((c >> 8) & 0xFF) - bg, db = (c & 0xFF) - bb;
                mask[i] = dr * dr + dg * dg + db * db > threshold;
            }
            return mask;
        }

        // Closes small gaps and specks (grow then shrink by 'radius' pixels).
        private static bool[] Close(bool[] m, int w, int h, int radius)
        {
            return Grow(Grow(m, w, h, radius, true), w, h, radius, false);
        }

        private static bool[] Grow(bool[] m, int w, int h, int radius, bool grow)
        {
            var o = new bool[m.Length];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    bool v = !grow;
                    for (int dy = -radius; dy <= radius && v == !grow; dy++)
                        for (int dx = -radius; dx <= radius; dx++)
                        {
                            int nx = x + dx, ny = y + dy;
                            bool s = nx >= 0 && ny >= 0 && nx < w && ny < h && m[ny * w + nx];
                            if (grow && s) { v = true; break; }
                            if (!grow && !s) { v = false; break; }
                        }
                    o[y * w + x] = v;
                }
            return o;
        }

        private static bool[] LargestShape(bool[] m, int w, int h)
        {
            var label = new int[m.Length];
            int best = 0, bestSize = 0, next = 0;
            var stack = new Stack<int>();
            for (int i = 0; i < m.Length; i++)
            {
                if (!m[i] || label[i] != 0) continue;
                next++;
                int size = 0;
                stack.Push(i);
                label[i] = next;
                while (stack.Count > 0)
                {
                    int p = stack.Pop();
                    size++;
                    int x = p % w, y = p / w;
                    if (x > 0 && m[p - 1] && label[p - 1] == 0) { label[p - 1] = next; stack.Push(p - 1); }
                    if (x < w - 1 && m[p + 1] && label[p + 1] == 0) { label[p + 1] = next; stack.Push(p + 1); }
                    if (y > 0 && m[p - w] && label[p - w] == 0) { label[p - w] = next; stack.Push(p - w); }
                    if (y < h - 1 && m[p + w] && label[p + w] == 0) { label[p + w] = next; stack.Push(p + w); }
                }
                if (size > bestSize) { bestSize = size; best = next; }
            }
            var o = new bool[m.Length];
            for (int i = 0; i < m.Length; i++) o[i] = label[i] == best && best != 0;
            return o;
        }

        // Anything the background can't reach from the photo's edge is part of the rim (fills the grip holes).
        private static bool[] FillHoles(bool[] m, int w, int h)
        {
            var outside = new bool[m.Length];
            var stack = new Stack<int>();
            void Seed(int i) { if (!m[i] && !outside[i]) { outside[i] = true; stack.Push(i); } }
            for (int x = 0; x < w; x++) { Seed(x); Seed((h - 1) * w + x); }
            for (int y = 0; y < h; y++) { Seed(y * w); Seed(y * w + w - 1); }
            while (stack.Count > 0)
            {
                int p = stack.Pop();
                int x = p % w, y = p / w;
                if (x > 0) Seed(p - 1);
                if (x < w - 1) Seed(p + 1);
                if (y > 0) Seed(p - w);
                if (y < h - 1) Seed(p + w);
            }
            var o = new bool[m.Length];
            for (int i = 0; i < m.Length; i++) o[i] = !outside[i];
            return o;
        }

        // ---- edge following and simplifying --------------------------------------------------

        // Moore-neighbour tracing around the outer boundary.
        private static List<PointF> FollowEdge(bool[] m, int w, int h)
        {
            bool At(int x, int y) => x >= 0 && y >= 0 && x < w && y < h && m[y * w + x];
            int sx = -1, sy = -1;
            for (int i = 0; i < m.Length && sx < 0; i++) if (m[i]) { sx = i % w; sy = i / w; }
            var pts = new List<PointF>();
            if (sx < 0) return pts;

            int[] ox = { -1, -1, 0, 1, 1, 1, 0, -1 };
            int[] oy = { 0, -1, -1, -1, 0, 1, 1, 1 };
            int cx = sx, cy = sy, dir = 4; // as if we arrived moving right (start is the top-left-most pixel)
            int limit = w * h * 4;
            do
            {
                pts.Add(new PointF(cx + 0.5f, cy + 0.5f));
                int start = (dir + 6) % 8, k;
                for (k = 0; k < 8; k++)
                {
                    int d = (start + k) % 8;
                    if (At(cx + ox[d], cy + oy[d])) { cx += ox[d]; cy += oy[d]; dir = d; break; }
                }
                if (k == 8) break; // a single pixel
            } while ((cx != sx || cy != sy) && --limit > 0);
            return pts;
        }

        private static List<PointF> Simplify(List<PointF> ring, float epsilon)
        {
            // Split the closed ring at its two furthest-apart points, simplify each half.
            int a = 0, b = 0;
            float best = -1;
            for (int i = 0; i < ring.Count; i += Math.Max(1, ring.Count / 200))
                for (int j = 0; j < ring.Count; j += Math.Max(1, ring.Count / 200))
                {
                    float dx = ring[i].X - ring[j].X, dy = ring[i].Y - ring[j].Y, d = dx * dx + dy * dy;
                    if (d > best) { best = d; a = i; b = j; }
                }
            if (a > b) { int t = a; a = b; b = t; }
            var first = ring.GetRange(a, b - a + 1);
            var second = new List<PointF>(ring.GetRange(b, ring.Count - b));
            second.AddRange(ring.GetRange(0, a + 1));
            var o = new List<PointF>();
            o.AddRange(Dp(first, epsilon));
            o.RemoveAt(o.Count - 1);
            o.AddRange(Dp(second, epsilon));
            o.RemoveAt(o.Count - 1);
            return o;
        }

        private static List<PointF> Dp(List<PointF> pts, float eps)
        {
            if (pts.Count < 3) return new List<PointF>(pts);
            var keep = new bool[pts.Count];
            keep[0] = keep[pts.Count - 1] = true;
            var stack = new Stack<(int, int)>();
            stack.Push((0, pts.Count - 1));
            while (stack.Count > 0)
            {
                var (s, e) = stack.Pop();
                float best = 0;
                int idx = -1;
                for (int i = s + 1; i < e; i++)
                {
                    float d = LineDistance(pts[i], pts[s], pts[e]);
                    if (d > best) { best = d; idx = i; }
                }
                if (idx >= 0 && best > eps)
                {
                    keep[idx] = true;
                    stack.Push((s, idx));
                    stack.Push((idx, e));
                }
            }
            var o = new List<PointF>();
            for (int i = 0; i < pts.Count; i++) if (keep[i]) o.Add(pts[i]);
            return o;
        }

        private static float LineDistance(PointF p, PointF a, PointF b)
        {
            float dx = b.X - a.X, dy = b.Y - a.Y;
            float len = (float)Math.Sqrt(dx * dx + dy * dy);
            if (len < 1e-6f) return (float)Math.Sqrt((p.X - a.X) * (p.X - a.X) + (p.Y - a.Y) * (p.Y - a.Y));
            return Math.Abs(dy * p.X - dx * p.Y + b.X * a.Y - b.Y * a.X) / len;
        }
    }
}
