// Return one grid-aligned rectangle per child, retaining the input ID order.
function arrange(properties, context) {
    const count=context.children.length
    if(!count)return []
    const grid=context.grid,columns=Math.min(count,properties.columns),rows=Math.ceil(count/columns)
    const gap=Math.max(0,Math.round(properties.spacing/grid))*grid
    const width=Math.max(grid,Math.floor((context.width-gap*(columns-1))/columns/grid)*grid)
    const height=Math.max(grid,Math.floor((context.height-gap*(rows-1))/rows/grid)*grid)
    return context.children.map((child,index)=>{
        const place=properties.reverse?count-index-1:index
        return {x:(place%columns)*(width+gap),y:Math.floor(place/columns)*(height+gap),width:width,height:height}
    })
}
