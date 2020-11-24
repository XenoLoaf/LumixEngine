#include "engine/allocators.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/thread.h"
#include "engine/universe.h"
#include "lua_script/lua_script_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"

using namespace Lumix;

static const ComponentType ENVIRONMENT_TYPE = Reflection::getComponentType("environment");
static const ComponentType LUA_SCRIPT_TYPE = Reflection::getComponentType("lua_script");

struct Runner final : OS::Interface
{
	Runner() 
		: m_allocator(m_main_allocator) 
	{
		if (!JobSystem::init(OS::getCPUsCount(), m_allocator)) {
			logError("Failed to initialize job system.");
		}
	}
	~Runner() { ASSERT(!m_universe); }

	void onResize() {
		if (!m_engine.get()) return;
		if (m_engine->getWindowHandle() == OS::INVALID_WINDOW) return;

		const OS::Rect r = OS::getWindowClientRect(m_engine->getWindowHandle());
		m_viewport.w = r.width;
		m_viewport.h = r.height;
	}

	void initRenderPipeline() {
		m_viewport.fov = degreesToRadians(60.f);
		m_viewport.far = 10'000.f;
		m_viewport.is_ortho = false;
		m_viewport.near = 0.1f;
		m_viewport.pos = {0, 0, 0};
		m_viewport.rot = Quat::IDENTITY;

		m_renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = m_engine->getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*m_renderer, pres, "APP", m_engine->getAllocator());

		while (m_engine->getFileSystem().hasWork()) {
			OS::sleep(100);
			m_engine->getFileSystem().processCallbacks();
		}

		m_pipeline->setUniverse(m_universe);
	}

	void initDemoScene() {
		const EntityRef env = m_universe->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_universe->createComponent(ENVIRONMENT_TYPE, env);
		m_universe->createComponent(LUA_SCRIPT_TYPE, env);
		
		RenderScene* render_scene = (RenderScene*)m_universe->getScene(crc32("renderer"));
		Environment& environment = render_scene->getEnvironment(env);
		environment.diffuse_intensity = 3;
		
		Quat rot;
		rot.fromEuler(Vec3(degreesToRadians(45.f), 0, 0));
		m_universe->setRotation(env, rot);
		
		LuaScriptScene* lua_scene = (LuaScriptScene*)m_universe->getScene(crc32("lua_script"));
		lua_scene->addScript(env, 0);
		lua_scene->setScriptPath(env, 0, Path("pipelines/atmo.lua"));
	}

	bool loadUniverse(const char* path) {
		OS::InputFile file;
		if (!file.open(path)) return false;

		OutputMemoryStream data(m_allocator);
		data.resize(file.size());
		if (!file.read(data.getMutableData(), data.size())) {
			logError("Failed to read universes/main/entities.unv");
			return false;
		}
		file.close();

		InputMemoryStream tmp(data);
		EntityMap entity_map(m_allocator);
		struct Header {
			u32 magic;
			i32 version;
			u32 hash;
			u32 engine_hash;
		} header;

		tmp.read(Ref(header));

		m_universe->setName("main");
		if (!m_engine->deserialize(*m_universe, tmp, Ref(entity_map))) {
			logError("Failed to deserialize universes/main/entities.unv");
			return false;
		}
		return true;
	}

	void onInit() override {
		Engine::InitArgs init_data;
		m_engine = Engine::create(init_data, m_allocator);

		m_universe = &m_engine->createUniverse(true);
		initRenderPipeline();
		
		if (!loadUniverse("universes/main/entities.unv")) {
			initDemoScene();
		}
		while (m_engine->getFileSystem().hasWork()) {
			OS::sleep(10);
			m_engine->getFileSystem().processCallbacks();
		}

		OS::showCursor(false);
		onResize();
		m_engine->startGame(*m_universe);
		Profiler::pause(false);
	}

	void shutdown() {
		m_shutting_down = true;
		m_engine->destroyUniverse(*m_universe);
		m_pipeline.reset();
		m_engine.reset();
		m_universe = nullptr;
	}

	void onEvent(const OS::Event& event) override {
		if (m_shutting_down) return;
		if (m_engine.get()) {
			InputSystem& input = m_engine->getInputSystem();
			input.injectEvent(event, 0, 0);
		}
		switch (event.type) {
			case OS::Event::Type::KEY:
				if (event.key.keycode == OS::Keycode::Z && event.key.down) {
					OS::OutputFile file;
					if (file.open("profile.lpd")) {
						OutputMemoryStream blob(m_allocator);
						Profiler::serialize(blob);
						(void)file.write(blob.data(), blob.size());
						file.close();
					}
				}
				break;	
			case OS::Event::Type::QUIT:
			case OS::Event::Type::WINDOW_CLOSE: 
				OS::quit();
				break;
			case OS::Event::Type::WINDOW_MOVE:
			case OS::Event::Type::WINDOW_SIZE:
				onResize();
				break;
		}
	}

	void onIdle() override {
		m_engine->update(*m_universe);

		EntityPtr camera = m_pipeline->getScene()->getActiveCamera();
		if (camera.isValid()) {
			int w = m_viewport.w;
			int h = m_viewport.h;
			m_viewport = m_pipeline->getScene()->getCameraViewport((EntityRef)camera);
			m_viewport.w = w;
			m_viewport.h = h;
		}

		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_renderer->frame();
	}

	DefaultAllocator m_main_allocator;
	Debug::Allocator m_allocator;
	UniquePtr<Engine> m_engine;
	Renderer* m_renderer = nullptr;
	Universe* m_universe = nullptr;
	UniquePtr<Pipeline> m_pipeline;
	Viewport m_viewport;
	bool m_shutting_down = false;
};

int main(int args, char* argv[])
{
	Profiler::setThreadName("Main thread");
	struct Data {
		Data() : semaphore(0, 1) {}
		Runner app;
		Semaphore semaphore;
	} data;

	JobSystem::runEx(&data, [](void* ptr) {
		Data* data = (Data*)ptr;

		OS::run(data->app);
		data->app.shutdown();

		data->semaphore.signal();
	}, nullptr, JobSystem::INVALID_HANDLE, 0);
	
	PROFILE_BLOCK("sleeping");
	data.semaphore.wait();

	return 0;
}
