/* Team abbreviations retain actual supplied Ringside outlines. Other
 * scorebug roles use the independently selected webfont faces. */
(function () {
  'use strict';
  var data = window.SNFWideGlyphData, ns = 'http://www.w3.org/2000/svg';
  function word(text) {
    var cursor = 0, bounds = null, parts = [];
    for (var i = 0; i < text.length; i++) {
      var g = data && data[text[i]];
      if (!g) return null;
      if (g.bounds) {
        var b = [g.bounds[0] + cursor, g.bounds[1], g.bounds[2] + cursor, g.bounds[3]];
        bounds = bounds ? [Math.min(bounds[0], b[0]), Math.min(bounds[1], b[1]),
          Math.max(bounds[2], b[2]), Math.max(bounds[3], b[3])] : b;
        parts.push({path:g.path, x:cursor});
      }
      cursor += g.advance;
    }
    return bounds ? {bounds:bounds, parts:parts} : null;
  }
  function fallback(el, text) {
    if (el.getAttribute('data-snf-glyphs') === '1') {
      /* A later unsupported value must return to real text without the
       * previous image's label or dimensions. The caller then fits it. */
      el.textContent = text;
      el.removeAttribute('role'); el.removeAttribute('aria-label');
      el.removeAttribute('data-snf-glyphs');
      el.style.width = ''; el.style.height = '';
    }
    return false;
  }
  window.SNFBroadcastGlyphs = {
    render:function (el, spec, text) {
      if (!document.createElementNS || spec.el.indexOf('.abbr') < 0) return fallback(el, text);
      var current = word(text), reference = word(spec.ref[0]);
      if (!current || !reference) return fallback(el, text);
      var b = current.bounds, r = reference.bounds;
      var sx = spec.ref[1] / (r[2] - r[0]), sy = spec.cap / (r[3] - r[1]);
      var width = (b[2] - b[0]) * sx, height = (b[3] - b[1]) * sy;
      var svg = document.createElementNS(ns, 'svg');
      svg.setAttribute('viewBox', b[0]+' '+b[1]+' '+(b[2]-b[0])+' '+(b[3]-b[1]));
      svg.setAttribute('width', width); svg.setAttribute('height', height);
      svg.setAttribute('preserveAspectRatio', 'none');
      svg.setAttribute('aria-hidden', 'true');
      svg.style.overflow = 'visible';
      var title = document.createElementNS(ns, 'title');
      title.textContent = text; svg.appendChild(title);
      current.parts.forEach(function (g) {
        var path = document.createElementNS(ns, 'path');
        path.setAttribute('d', g.path); path.setAttribute('transform', 'translate('+g.x+' 0)');
        path.setAttribute('fill', 'currentColor'); path.setAttribute('stroke', 'currentColor');
        path.setAttribute('stroke-width', '.5'); path.setAttribute('vector-effect', 'non-scaling-stroke');
        path.setAttribute('stroke-linejoin', 'round'); svg.appendChild(path);
      });
      el.textContent = ''; el.appendChild(svg);
      /* A generic div cannot be named with aria-label. Treat the outline
       * composite as one labelled image while its title preserves text. */
      el.setAttribute('role', 'img'); el.setAttribute('aria-label', text);
      el.setAttribute('data-snf-glyphs', '1');
      el.style.top = (spec.top + (b[1]-r[1])*sy) + 'px';
      el.style.left = (spec.anchor === 'right' ? spec.x-width : spec.anchor === 'center' ? spec.x-width/2 : spec.x) + 'px';
      el.style.width = width+'px'; el.style.height = height+'px'; el.style.transform = 'none';
      return true;
    }
  };
})();
