// model.bin: "KHUD", u32 version, u32 sections; each section = 4-char tag, u32 byte count, payload.
//   MCEL / FCEL / GLYP  templates: u32 dim, u32 nchars, then per char: u8 char, u32 count, float[count][dim]
//   MPAR / FPAR         pairs:     u32 dim, u32 npairs, then per pair: u8 a, u8 b, float t, float[dim] w
// Written by tools/ocrlab/train_model.py from cells the lab harness cut with this very code.
#include "internal.h"
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>

namespace hud {
using namespace detail;

namespace {
struct Reader {
	const uint8_t *p, *end;
	bool ok = true;
	template<typename T> T get()
	{
		T v{};
		if (end - p < (ptrdiff_t)sizeof(T)) {
			ok = false;
			return v;
		}
		memcpy(&v, p, sizeof(T));
		p += sizeof(T);
		return v;
	}
	bool floats(float *out, size_t n)
	{
		if ((size_t)(end - p) < n * sizeof(float)) {
			ok = false;
			return false;
		}
		memcpy(out, p, n * sizeof(float));
		p += n * sizeof(float);
		return true;
	}
};

bool readTemplates(Reader &r, Templates &t)
{
	t.dim = (int)r.get<uint32_t>();
	uint32_t n = r.get<uint32_t>();
	if (!r.ok || t.dim <= 0 || t.dim > 4096 || n > 128)
		return false;
	for (uint32_t i = 0; i < n && r.ok; i++) {
		char c = (char)r.get<uint8_t>();
		uint32_t k = r.get<uint32_t>();
		if (!r.ok || k > 64)
			return false;
		auto &v = t.byChar[c];
		for (uint32_t j = 0; j < k; j++) {
			std::vector<float> f((size_t)t.dim);
			if (!r.floats(f.data(), f.size()))
				return false;
			v.push_back(std::move(f));
		}
	}
	return r.ok;
}

bool readPairs(Reader &r, Pairs &ps)
{
	uint32_t dim = r.get<uint32_t>(), n = r.get<uint32_t>();
	if (!r.ok || dim == 0 || dim > 4096 || n > 4096)
		return false;
	for (uint32_t i = 0; i < n && r.ok; i++) {
		char a = (char)r.get<uint8_t>(), b = (char)r.get<uint8_t>();
		Pair p;
		p.t = r.get<float>();
		p.w.resize(dim);
		if (!r.floats(p.w.data(), dim))
			return false;
		ps.byPair[{a, b}] = std::move(p);
	}
	return r.ok;
}
} // namespace

std::shared_ptr<const Model> loadModelFromMemory(const uint8_t *data, size_t size, std::string *err)
{
	auto fail = [&](const char *why) -> std::shared_ptr<const Model> {
		if (err)
			*err = why;
		return nullptr;
	};
	Reader r{data, data + size};
	char magic[4];
	for (char &c : magic)
		c = (char)r.get<uint8_t>();
	if (!r.ok || memcmp(magic, "KHUD", 4) != 0)
		return fail("not a HUD model file");
	uint32_t version = r.get<uint32_t>();
	if (version != 1)
		return fail("unsupported HUD model version");
	uint32_t sections = r.get<uint32_t>();
	auto m = std::make_shared<Model>();
	for (uint32_t s = 0; s < sections && r.ok; s++) {
		char tag[5] = {0};
		for (int i = 0; i < 4; i++)
			tag[i] = (char)r.get<uint8_t>();
		uint32_t bytes = r.get<uint32_t>();
		if (!r.ok || (size_t)(r.end - r.p) < bytes)
			return fail("truncated HUD model");
		Reader sub{r.p, r.p + bytes};
		bool ok = true;
		if (!strcmp(tag, "MCEL"))
			ok = readTemplates(sub, m->moneyCells);
		else if (!strcmp(tag, "FCEL"))
			ok = readTemplates(sub, m->feedCells);
		else if (!strcmp(tag, "GLYP"))
			ok = readTemplates(sub, m->glyphs);
		else if (!strcmp(tag, "MPAR"))
			ok = readPairs(sub, m->moneyPairs);
		else if (!strcmp(tag, "FPAR"))
			ok = readPairs(sub, m->feedPairs);
		if (!ok)
			return fail("damaged HUD model section");
		r.p += bytes;
	}
	if (m->moneyCells.dim != CELL)
		return fail("HUD model has no money templates");
	return m;
}

std::shared_ptr<const Model> loadModel(const std::string &path, std::string *err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		if (err)
			*err = "cannot open " + path;
		return nullptr;
	}
	std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return loadModelFromMemory(buf.data(), buf.size(), err);
}

} // namespace hud
