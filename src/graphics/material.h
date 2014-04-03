#pragma once


#include "core/array.h"
#include "core/resource.h"


namespace Lux
{
namespace FS
{
	class FileSystem;
	class IFile;
}

class ResourceManager;
class Shader;
class Texture;


class Material : public Resource
{
	friend class MaterialManager;
public:
	void apply();
	void setShader(Shader* shader) { m_shader = shader; }
	void addTexture(Texture* texture) { m_textures.push(texture); }
	Shader* getShader() { return m_shader; }

private:
	Material(const Path& path, ResourceManager& resource_manager)
		: Resource(path, resource_manager)
		, m_shader()
		, m_textures()
	{ }

	~Material()
	{ }

	virtual void doUnload(void) override;
	virtual FS::ReadCallback getReadCallback() override;

	void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);
private:
	Shader*	m_shader;
	Array<Texture*> m_textures;
};

} // ~namespace Lux
