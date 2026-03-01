namespace batap
{
struct Context;
struct IApp
{
    virtual ~IApp() = default;
    virtual void start(Context& ctx) = 0;
    virtual void update(Context& ctx) = 0;
    virtual void shutdown(Context& ctx) = 0;
};
}  // namespace batap
