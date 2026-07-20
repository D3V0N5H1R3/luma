// Extract curated Lucide icons as a compact JS object for embedding.
const fs = require('fs');
const path = require('path');

// Load and evaluate the UMD bundle to get the exports
const code = fs.readFileSync(path.join(__dirname, 'lucide.min.js'), 'utf8');

// The UMD sets global.lucide, simulate it
const fakeGlobal = {};
const fakeModule = {};
const fn = new Function('global', 'self', 'globalThis', 'exports', 'module', 'define',
    code + '\nreturn global.lucide || exports;');
const lucide = fn(fakeGlobal, fakeGlobal, fakeGlobal, {}, fakeModule, undefined);
const icons = lucide.icons || fakeGlobal.lucide?.icons || {};

// Curated list of useful UI icons (kebab-case keys as used by Lucide)
const wanted = [
    'AlertCircle','AlertTriangle','ArrowDown','ArrowLeft','ArrowRight','ArrowUp',
    'Bell','Bold','Bookmark','Calendar','Check','CheckCircle',
    'ChevronDown','ChevronLeft','ChevronRight','ChevronUp',
    'Circle','Clipboard','Clock','Cloud','Code','Copy',
    'CreditCard','Database','Download','Edit','ExternalLink','Eye','EyeOff',
    'File','FileText','Filter','Flag','Folder','Globe',
    'Heart','HelpCircle','Home','Image','Inbox','Info',
    'Italic','Key','Layers','Link','List','Lock',
    'LogIn','LogOut','Mail','MapPin','Menu','MessageCircle',
    'Minus','Moon','MoreHorizontal','MoreVertical','Music',
    'Package','Pause','PenLine','Phone','Play','Plus',
    'Power','Printer','RefreshCw','Save','Search','Send',
    'Settings','Share','Shield','ShoppingCart','Sliders','Smile',
    'Square','Star','Sun','Table','Tag','Terminal',
    'ThumbsDown','ThumbsUp','Trash','Type','Underline',
    'Undo','Unlock','Upload','User','Users','Video','Volume',
    'Wifi','X','Zap','ZoomIn','ZoomOut',
    'Activity','Aperture','Award','Battery','Book','Box','Briefcase',
    'Camera','Cast','Cpu','Disc','Feather','Headphones','Mic',
    'Monitor','Navigation','Radio','Server','Speaker','Smartphone',
    'Wrench','Truck','Tv','Watch','Wind',
    'Sparkles','Hash','TrendingUp','Compass','CheckSquare'
];

// Convert PascalCase to kebab-case for lookup
function toKebab(s) {
    return s.replace(/([a-z])([A-Z])/g, '$1-$2').replace(/([A-Z]+)([A-Z][a-z])/g, '$1-$2').toLowerCase();
}

// Extract just the child elements (paths, circles, etc) — skip the "svg" wrapper and `h` attrs
const result = {};
let found = 0, missing = [];

for (const name of wanted) {
    const kebab = toKebab(name);
    const icon = icons[name];
    if (icon) {
        // icon is ["svg", {attrs}, [children]]
        const children = icon[2] || [];
        // Each child is ["tagName", {attrs}] or ["tagName", {attrs}, [subchildren]]
        result[kebab] = children.map(c => {
            if (c.length === 2) return [c[0], c[1]];
            return c; // include sub-children if present
        });
        found++;
    } else {
        missing.push(name);
    }
}

console.log(`Found: ${found}/${wanted.length}`);
if (missing.length) console.log(`Missing: ${missing.join(', ')}`);

// Output as compact JSON
const json = JSON.stringify(result);
console.log(`Output size: ${json.length} chars`);
fs.writeFileSync(path.join(__dirname, 'lucide-subset.json'), json);

// Also generate the JS source split into two halves for C++ constexpr embedding.
// Each half will be a partial JSON string that gets concatenated.
const entries = Object.entries(result);
const half = Math.ceil(entries.length / 2);
const part1 = Object.fromEntries(entries.slice(0, half));
const part2 = Object.fromEntries(entries.slice(half));
const js1 = JSON.stringify(part1);
const js2 = JSON.stringify(part2);
console.log(`Split: part1=${js1.length} chars (${half} icons), part2=${js2.length} chars (${entries.length - half} icons)`);
console.log(`Both under 16380: ${js1.length < 16380 && js2.length < 16380}`);
fs.writeFileSync(path.join(__dirname, 'lucide-icons-part1.json'), js1);
fs.writeFileSync(path.join(__dirname, 'lucide-icons-part2.json'), js2);
console.log('Written split files');
