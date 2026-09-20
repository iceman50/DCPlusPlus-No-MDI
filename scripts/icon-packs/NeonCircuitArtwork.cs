// Copyright (C) 2026 iceman50
// Neon-specific silhouettes and a single, composited light halo. Shared semantic
// glyphs retain the same optical grid and state badges as the other collections.
using System;
using System.Drawing;
using System.Drawing.Imaging;

public static partial class IconPackRenderer {
    private static readonly Color NeonPink = Hex("ff64df");

    private static void NeonFolder(Canvas c, string badge) {
        c.Line(c.P.Blue,3,10,3,5,9,5,12,8,20,8);
        c.Polygon(c.P.Blue,true,3,10,21,10,19,20,5,20);
        if(badge != null) c.Badge(badge,badge == "star" ? c.P.Gold : c.P.Blue);
    }

    private static void NeonDocument(Canvas c, string badge, bool lines) {
        c.Polygon(c.P.Blue,true,5,3,14,3,19,8,19,19,17,21,5,21);
        c.Line(c.P.Violet,14,3,14,8,19,8);
        if(lines) {
            c.Line(c.P.Ink,8,12,15,12);
            if(!c.Small) c.Line(c.P.Violet,8,16,12,16);
        }
        if(badge != null) c.Badge(badge,badge == "clock" ? c.P.Gold : c.P.Blue);
    }

    private static void NeonHub(Canvas c, bool connected) {
        Color wire = connected ? c.P.Blue : c.P.Muted;
        c.Line(wire,12,4,12,8); c.Line(wire,5,18,9,14); c.Line(wire,19,18,15,14);
        c.Circle(wire,12,11,3.5f,true);
        c.Dot(connected ? c.P.Violet : wire,12,3.5f,1.5f);
        c.Circle(wire,4.5f,19,2,false); c.Circle(wire,19.5f,19,2,false);
        if(!connected) c.Badge("minus",c.P.Muted);
    }

    private static bool DrawNeon(Canvas c, string name) {
        Palette p = c.P;
        switch(name) {
            case "publichubs":
                c.Circle(p.Blue,12,12,8.5f,true); c.Arc(p.Blue,8,3.5f,8,17,0,360);
                c.Line(p.Violet,3.5f,12,20.5f,12); break;
            case "search":
                c.Circle(p.Blue,10,10,6.4f,true); c.Line(p.Violet,15,15,21,21);
                if(!c.Small) c.Arc(p.Ink,6.5f,6.5f,7,7,205,65); break;
            case "directory": NeonFolder(c,null); break;
            case "favoritedirs": NeonFolder(c,"star"); break;
            case "opendldir": NeonFolder(c,"down"); break;
            case "file": NeonDocument(c,null,false); break;
            case "adlsearch": NeonDocument(c,"search",false); break;
            case "changelog": NeonDocument(c,"clock",true); break;
            case "openfilelist": NeonDocument(c,"up",true); break;
            case "openownfilelist": NeonDocument(c,"user",true); break;
            case "logs": NeonDocument(c,null,true); break;
            case "hubon": NeonHub(c,true); break;
            case "huboff": NeonHub(c,false); break;
            case "download": Arrow(c,p.Blue,false,true); break;
            case "upload": Arrow(c,NeonPink,true,true); break;
            case "finisheddl": Arrow(c,p.Blue,false,true); c.Badge("check",p.Green); break;
            case "finishedul": Arrow(c,NeonPink,true,true); c.Badge("check",p.Green); break;
            case "chat": case "balloon":
                c.Polygon(p.Blue,true,5,4,19,4,21,6,21,16,19,18,10,18,5,21,5,18,3,18,3,6);
                if(name == "chat") { c.Line(p.Ink,7,9,17,9); c.Line(p.Violet,7,13,13,13); }
                else { c.Dot(p.Violet,8,11,1); c.Dot(p.Ink,12,11,1); c.Dot(p.Violet,16,11,1); } break;
            case "plugins":
                c.Box(p.Violet,6,6,12,12,2,true);
                c.Line(p.Violet,9,3,9,6); c.Line(p.Violet,15,3,15,6);
                c.Line(p.Violet,9,18,9,21); c.Line(p.Violet,15,18,15,21);
                c.Line(p.Violet,3,9,6,9); c.Line(p.Violet,3,15,6,15);
                c.Line(p.Violet,18,9,21,9); c.Line(p.Violet,18,15,21,15);
                c.Line(p.Blue,9,12,15,12); c.Line(p.Blue,12,9,12,15); break;
            case "netstats":
                c.Line(p.Muted,3,4,3,21,21,21);
                c.Line(p.Blue,5,14,8,14,11,7,14,17,17,10,21,10);
                if(!c.Small) c.Dot(p.Violet,21,10,1.1f); break;
            case "notifications":
                c.Arc(NeonPink,6,4,12,14,180,180); c.Line(NeonPink,6,11,5,16,3,18,21,18,19,16,18,11);
                c.Arc(p.Ink,10,17,4,4,0,180); c.Dot(NeonPink,12,3,1); break;
            case "notepad":
                c.Box(p.Blue,4,4,15,17,1.5f,true); c.Line(p.Violet,8,3,8,6); c.Line(p.Violet,15,3,15,6);
                c.Line(p.Ink,8,10,15,10); c.Line(p.Blue,8,14,13,14); c.Line(NeonPink,15,20,21,12); break;
            case "donate":
                c.Curve(NeonPink,12,21,9,18,3,14,3,8); c.Curve(NeonPink,3,8,3,2,10,2,12,7);
                c.Curve(NeonPink,12,7,14,2,21,2,21,8); c.Curve(NeonPink,21,8,21,14,15,18,12,21); break;
            case "clock":
                c.Circle(p.Blue,12,12,8.5f,true); c.Line(p.Ink,12,6.5f,12,12); c.Line(p.Violet,12,12,16,14); break;
            case "exec":
                c.Box(p.Blue,3,4,18,16,2,true); c.Line(p.Ink,7,9,10,12,7,15); c.Line(p.Violet,13,15,17,15); break;
            case "traypm":
                c.Box(p.Blue,3,5,18,14,2,true); c.Line(p.Violet,4,7,12,13,20,7); c.Badge("dot",NeonPink); break;
            case "slots": case "slotsfull":
                c.Box(p.Blue,5,3,14,18,2,true);
                c.Line(name == "slotsfull" ? p.Red : p.Green,8,7,16,7);
                c.Line(name == "slotsfull" ? p.Red : p.Green,8,12,16,12);
                c.Line(p.Muted,8,17,16,17);
                if(name == "slotsfull") c.Badge("minus",p.Red); break;
            case "uploadfiltering":
                c.Polygon(p.Blue,true,3,4,21,4,14,12,14,19,10,21,10,12); c.Badge("up",NeonPink); break;
            default: return false;
        }
        return true;
    }

    // Blur premultiplied RGB and alpha together, then put the light behind the
    // finished artwork. A single pass avoids bright blobs at stroke junctions.
    // No halo at 16-22 px. At larger sizes it stays local to the luminous tubes.
    private static Bitmap NeonHalo(Bitmap source) {
        int size = source.Width, length = size * size * 4;
        var pixels = new double[length];
        var horizontal = new double[length];
        double sigma = Math.Max(.6, size * .025);
        int radius = (int)Math.Ceiling(sigma * 2.5);
        var kernel = new double[radius * 2 + 1];
        double weightSum = 0;
        for(int i = -radius; i <= radius; i++) { kernel[i + radius] = Math.Exp(-i * i / (2 * sigma * sigma)); weightSum += kernel[i + radius]; }
        for(int i = 0; i < kernel.Length; i++) kernel[i] /= weightSum;
        for(int y = 0; y < size; y++) for(int x = 0; x < size; x++) {
            Color c = source.GetPixel(x,y); int i = (y * size + x) * 4;
            double alpha = c.A / 255.0;
            // Dark badge symbols must not emit light.
            if(Math.Max(c.R,Math.Max(c.G,c.B)) < 100) alpha = 0;
            pixels[i] = c.R * alpha; pixels[i+1] = c.G * alpha; pixels[i+2] = c.B * alpha; pixels[i+3] = alpha;
        }
        for(int y = 0; y < size; y++) for(int x = 0; x < size; x++) {
            int target = (y * size + x) * 4;
            for(int k = -radius; k <= radius; k++) {
                int sx = x + k; if(sx < 0 || sx >= size) continue;
                int input = (y * size + sx) * 4; double weight = kernel[k + radius];
                for(int channel = 0; channel < 4; channel++) horizontal[target+channel] += pixels[input+channel] * weight;
            }
        }
        var output = new Bitmap(size,size,PixelFormat.Format32bppArgb);
        for(int y = 0; y < size; y++) for(int x = 0; x < size; x++) {
            double r = 0, g = 0, b = 0, a = 0;
            for(int k = -radius; k <= radius; k++) {
                int sy = y + k; if(sy < 0 || sy >= size) continue;
                int input = (sy * size + x) * 4; double w = kernel[k + radius];
                r += horizontal[input] * w; g += horizontal[input+1] * w;
                b += horizontal[input+2] * w; a += horizontal[input+3] * w;
            }
            Color top = source.GetPixel(x,y);
            double topA = top.A / 255.0, haloA = a * .48, alpha = topA + haloA * (1-topA);
            if(alpha < .5 / 255) { output.SetPixel(x,y,Color.Transparent); continue; }
            double contribution = .48 * (1-topA);
            output.SetPixel(x,y,Color.FromArgb(Byte(alpha * 255),
                Byte((top.R * topA + r * contribution) / alpha),
                Byte((top.G * topA + g * contribution) / alpha),
                Byte((top.B * topA + b * contribution) / alpha)));
        }
        return output;
    }

    private static byte Byte(double value) { return (byte)Math.Max(0,Math.Min(255,(int)Math.Round(value))); }
}
